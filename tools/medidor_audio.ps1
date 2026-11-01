# Medidor de audio (30/10): pico REAL (0..1) que Windows ve en la sesion de audio de un proceso (IAudioMeterInformation).
# Mide lo que de verdad sale por el altavoz, incluida la salida winmm de la musica PS2 que el volcado dbz3_audio_dump no recoge.
# Uso: powershell -File medidor_audio.ps1 -ProcId PID -Seconds N -Out fichero   (una linea por 0,25 s: "segundos_epoch pico")
param([int]$Seconds=240,[string]$Out="",[int]$ProcId=0)
Add-Type -TypeDefinition @"
using System; using System.Runtime.InteropServices;
[ComImport, Guid("BCDE0395-E52F-467C-8E3D-C4579291692E")] class MMDevEnum {}
[ComImport, Guid("A95664D2-9614-4F35-A746-DE8DB63617E6"), InterfaceType(ComInterfaceType.InterfaceIsIUnknown)] interface IMMDeviceEnumerator { int EnumAudioEndpoints(int f,int m,out IntPtr d); int GetDefaultAudioEndpoint(int f,int r,out IMMDevice d); }
[ComImport, Guid("D666063F-1587-4E43-81F1-B948E807363F"), InterfaceType(ComInterfaceType.InterfaceIsIUnknown)] interface IMMDevice { int Activate(ref Guid id,int ctx,IntPtr p,[MarshalAs(UnmanagedType.IUnknown)] out object o); }
[ComImport, Guid("77AA99A0-1BD6-484F-8BC7-2C654C9A9B6F"), InterfaceType(ComInterfaceType.InterfaceIsIUnknown)] interface IAudioSessionManager2 { int a(); int b(); int GetSessionEnumerator(out IAudioSessionEnumerator e); }
[ComImport, Guid("E2F5BB11-0570-40CA-ACDD-3AA01277DEE8"), InterfaceType(ComInterfaceType.InterfaceIsIUnknown)] interface IAudioSessionEnumerator { int GetCount(out int n); int GetSession(int i,out IAudioSessionControl2 s); }
[ComImport, Guid("bfb7ff88-7239-4fc9-8fa2-07c950be9c6d"), InterfaceType(ComInterfaceType.InterfaceIsIUnknown)] interface IAudioSessionControl2 { int a(); int b(); int c(); int d(); int e(); int f(); int g(); int h(); int i(); int j(); int k(); int GetProcessId(out uint p); }
[ComImport, Guid("C02216F6-8C67-4B5B-9D00-D008E73E0064"), InterfaceType(ComInterfaceType.InterfaceIsIUnknown)] interface IAudioMeterInformation { int GetPeakValue(out float p); }
public static class Meter {
  public static string Peaks(string procName) {
    var en=(IMMDeviceEnumerator)new MMDevEnum(); IMMDevice dev; en.GetDefaultAudioEndpoint(0,1,out dev);
    Guid g=new Guid("77AA99A0-1BD6-484F-8BC7-2C654C9A9B6F"); object o; dev.Activate(ref g,23,IntPtr.Zero,out o);
    var mgr=(IAudioSessionManager2)o; IAudioSessionEnumerator se; mgr.GetSessionEnumerator(out se); int n; se.GetCount(out n);
    string r="";
    for(int i=0;i<n;i++){ IAudioSessionControl2 c; se.GetSession(i,out c); uint pid; c.GetProcessId(out pid);
      string nm=""; try{ nm=System.Diagnostics.Process.GetProcessById((int)pid).ProcessName;}catch{}
      float p=0; try{ ((IAudioMeterInformation)c).GetPeakValue(out p);}catch{}
      r+=nm+"("+pid+")="+p.ToString("0.0000")+" "; }
    return r; }
}
"@
$t=Get-Date; $buf=New-Object System.Collections.Generic.List[string]
while(((Get-Date)-$t).TotalSeconds -lt $Seconds){
  $pk=0.0; $all=[Meter]::Peaks("dbz3")
  foreach($m in [regex]::Matches($all,'\((\d+)\)=([\d,.]+)')){ if([int]$m.Groups[1].Value -eq $ProcId){ $pk=[double]::Parse($m.Groups[2].Value.Replace(',','.'),[Globalization.CultureInfo]::InvariantCulture) } }
  $buf.Add(("{0:0.00} {1:0.0000}" -f ([DateTimeOffset]::UtcNow.ToUnixTimeMilliseconds()/1000.0),$pk).Replace(',','.'))
  if($buf.Count % 20 -eq 0){ [IO.File]::WriteAllLines($Out,$buf) }
  Start-Sleep -Milliseconds 250 }
[IO.File]::WriteAllLines($Out,$buf)
