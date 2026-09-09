<#
.SYNOPSIS
  lab_f0.ps1 - Laboratorio F0: baseline reproducible + inventario de mods +
  clasificacion automatica de logs para DBZ Budokai 3 HD Collection.

.DESCRIPTION
  Automatiza el sprint F0 de la HOJA_DE_RUTA_ACELERADA.md:
    * Manifest: SHA256 de exe/DLLs/AFS + MD5 del default.xex (deteccion de
      region US/EU) + comprobacion de que rexruntime.dll NO es stale (marcador
      AfsGetVirtualTable presente).
    * Inventario de mods: activos (.disabled ausente) vs inactivos, y que
      entradas AFS overridean cada mod activo (detecta contaminacion de tests:
      dos mods activos sobre la misma entrada del mismo AFS).
    * Clasificacion de logs: por cada dbz3_*.log resume AFS OVERRIDE HIT/MISS,
      crashes (UNHANDLED EXCEPTION), errores de apertura y rango temporal.
  El resultado se escribe en <OutDir> y un resumen legible en exit_report.txt.

.EXAMPLE
  tools\lab_f0.ps1                       # todo (manifest + inventory + logs)
  tools\lab_f0.ps1 -OutDir out\analysis\f0 -Mode ClassifyLogs
  tools\lab_f0.ps1 -DryRun               # no escribe nada, solo informa
#>
param(
    [string]$OutDir = "out\analysis\f0",
    [ValidateSet("All", "Manifest", "Inventory", "ClassifyLogs")]
    [string]$Mode = "All",
    [switch]$DryRun,
    [string]$BuildDir = ""   # override del build (por defecto out\build\win-amd64-release)
)

$ErrorActionPreference = "Stop"

$ROOT = Split-Path -Parent $PSScriptRoot
if (-not $BuildDir) { $BuildDir = Join-Path $ROOT "out\build\win-amd64-release" }
if (-not (Test-Path -LiteralPath $BuildDir)) {
    Write-Error "BuildDir no existe: $BuildDir"
}

$LOGSDIR = Join-Path $BuildDir "logs"
$MODSDIR = Join-Path $BuildDir "mods"
$XEX = Join-Path $ROOT "default.xex"
$ABS_OUT = if ([System.IO.Path]::IsPathRooted($OutDir)) { $OutDir } else { Join-Path $ROOT $OutDir }

# ---- hashing helpers ---------------------------------------------------------
function Get-HashOf([string]$Path, [string]$Algo = "SHA256") {
    if (-not (Test-Path -LiteralPath $Path)) { return $null }
    $h = [System.Security.Cryptography.HashAlgorithm]::Create($Algo)
    try {
        $fs = [System.IO.File]::OpenRead($Path)
        try {
            $bytes = $h.ComputeHash($fs)
            return [BitConverter]::ToString($bytes).Replace("-", "").ToLowerInvariant()
        } finally { $fs.Dispose() }
    } finally { $h.Dispose() }
}

function Get-Region([string]$XexPath) {
    $md5 = Get-HashOf $XexPath "MD5"
    if (-not $md5) { return "unknown" }
    if ($md5 -eq "a53e324b5d2a65ebcbf648e4f85a7271") { return "us" }
    if ($md5 -eq "c37eb979b762da0ab5b8c9ba8037ce4e") { return "eu" }
    return "other"
}

function Assert-OutDir {
    if ($DryRun) { return }
    New-Item -ItemType Directory -Path $ABS_OUT -Force | Out-Null
}

function Write-File([string]$Name, [string]$Content) {
    if ($DryRun) { Write-Host "[dry-run] escribiria: $(Join-Path $ABS_OUT $Name)" ; return }
    Set-Content -LiteralPath (Join-Path $ABS_OUT $Name) -Value $Content -Encoding UTF8
}

# ---- Manifest ----------------------------------------------------------------
function Invoke-Manifest {
    Write-Host "== Manifest =="
    $lines = @()
    $lines += "dbz3_f0_manifest"
    $lines += "generated: $(Get-Date -Format o)"
    $lines += "root: $ROOT"
    $lines += "build: $BuildDir"

    # region
    $region = Get-Region $XEX
    $xexMd5 = Get-HashOf $XEX "MD5"
    if (-not $xexMd5) { $xexMd5 = "missing" }
    $lines += "default.xex: $XEX md5=$xexMd5 region=$region"

    # core binaries
    foreach ($name in @("dbz3.exe", "rexruntime.dll", "rexgpu-xenos.dll", "amd_fidelityfx_dx12.dll", "TracyClient.dll")) {
        $p = Join-Path $BuildDir $name
        $h = Get-HashOf $p
        $sz = if (Test-Path -LiteralPath $p) { (Get-Item -LiteralPath $p).Length } else { -1 }
        $lines += "${name}: sha256=$h size=$sz"
    }

    # rexruntime stale check (marcador critico del SDK)
    $rr = Join-Path $BuildDir "rexruntime.dll"
    if (Test-Path -LiteralPath $rr) {
        $marker = Select-String -LiteralPath $rr -Pattern "AfsGetVirtualTable" -SimpleMatch -Quiet
        $lines += "rexruntime marker AfsGetVirtualTable: $(if ($marker) {'PRESENTE'} else {'AUSENTE (STALE - recopiar DLL del SDK)'})"
    }

    # AFS us/eu
    foreach ($reg in @("us", "eu")) {
        foreach ($afs in @("data_cmn.afs", "data_eng.afs", "data_ger.afs", "data_spn.afs", "data_fra.afs", "data_ita.afs", "data_usi.afs", "data_jpn.afs", "adx_jpn.afs", "adx_usa.afs", "adx_us.afs", "lang_jpn.afs", "lang_usa.afs", "data_yah.afs")) {
            $p = Join-Path $ROOT "$reg\$afs"
            if (Test-Path -LiteralPath $p) {
                $h = Get-HashOf $p
                $sz = (Get-Item -LiteralPath $p).Length
                $lines += "${reg}/${afs}: sha256=$h size=$sz"
            }
        }
    }

    Write-File "manifest.json" ($lines -join "`n")
    Write-File "manifest.txt" ($lines -join "`n")
    $lines | ForEach-Object { Write-Host "  $_" }
}

# ---- Mods inventory ----------------------------------------------------------
function Invoke-Inventory {
    Write-Host "== Inventario de mods =="
    $lines = @()
    if (-not (Test-Path -LiteralPath $MODSDIR)) {
        $lines += "no hay carpeta mods en $MODSDIR"
        Write-File "mods_inventory.txt" ($lines -join "`n")
        return
    }
    $lines += "mods root: $MODSDIR"
    $conflict = @{}
    foreach ($mod in Get-ChildItem -LiteralPath $MODSDIR -Directory) {
        $marker = Join-Path $mod.FullName ".disabled"
        $state = if (Test-Path -LiteralPath $marker) { "INACTIVO" } else { "ACTIVO" }
        $lines += "mod: $($mod.Name) [$state]"
        if ($state -ne "ACTIVO") { continue }
        # que entradas overridea
        foreach ($regionDir in @("us", "eu")) {
            $rd = Join-Path $mod.FullName $regionDir
            if (-not (Test-Path -LiteralPath $rd)) { continue }
            foreach ($afsDir in Get-ChildItem -LiteralPath $rd -Directory) {
                foreach ($entryDir in Get-ChildItem -LiteralPath $afsDir.FullName -Directory) {
                    $entry = [int]$entryDir.Name
                    $key = "$regionDir/$($afsDir.Name):$entry"
                    $lines += "  override: $key  ($(Join-Path $entryDir.FullName '<file>'))"
                    if (-not $conflict.ContainsKey($key)) { $conflict[$key] = @() }
                    $conflict[$key] += $mod.Name
                }
            }
        }
    }
    $warn = @()
    foreach ($k in $conflict.Keys) {
        if ($conflict[$k].Count -gt 1) {
            $warn += "CONTAMINACION: $k servido por $($conflict[$k] -join ', ') (gana el alfabetico)"
        }
    }
    if ($warn.Count) {
        $lines += ""
        $lines += "!! AVISOS (contaminacion de tests):"
        $lines += $warn
    } else {
        $lines += ""
        $lines += "sin conflictos de override detectados"
    }
    Write-File "mods_inventory.txt" ($lines -join "`n")
    $lines | ForEach-Object { Write-Host "  $_" }
}

# ---- Log classification ------------------------------------------------------
function Invoke-ClassifyLogs {
    Write-Host "== Clasificacion de logs =="
    $lines = @()
    if (-not (Test-Path -LiteralPath $LOGSDIR)) {
        $lines += "no hay carpeta de logs: $LOGSDIR"
        Write-File "logs_classified.txt" ($lines -join "`n")
        return
    }
    $logs = Get-ChildItem -LiteralPath $LOGSDIR -Filter "dbz3_*.log" | Sort-Object LastWriteTime -Descending
    $lines += "logs: $($logs.Count) en $LOGSDIR"
    foreach ($l in $logs) {
        $hits = (Select-String -LiteralPath $l.FullName -Pattern "AFS OVERRIDE HIT" | Measure-Object).Count
        $hitsFolder = (Select-String -LiteralPath $l.FullName -Pattern "AFS OVERRIDE HIT \(folder\)" | Measure-Object).Count
        $miss = (Select-String -LiteralPath $l.FullName -Pattern "AFS OVERRIDE MISS" | Measure-Object).Count
        $ex = Select-String -LiteralPath $l.FullName -Pattern "UNHANDLED EXCEPTION" | Select-Object -Last 1
        $openFail = (Select-String -LiteralPath $l.FullName -Pattern "Open FAILED" | Measure-Object).Count
        $first = (Select-String -LiteralPath $l.FullName -Pattern "^\[" | Select-Object -First 1).Line
        $last = (Select-String -LiteralPath $l.FullName -Pattern "^\[" | Select-Object -Last 1).Line
        $t0 = if ($first -match "\[([0-9:\-\. ]+)\]") { $matches[1] } else { "?" }
        $t1 = if ($last -match "\[([0-9:\-\. ]+)\]") { $matches[1] } else { "?" }
        $crash = if ($ex) {
            if ($ex.Line -match "Code=(0x[0-9A-Fa-f]+) Addr=(0x[0-9A-Fa-f]+)") {
                "CRASH code=$($matches[1]) addr=$($matches[2])"
            } else { "CRASH (sin code parseable)" }
        } else { "ok" }
        $lines += "{0}: hits={1} folder={2} miss={3} openfail={4} [{5}..{6}] {7}" -f `
            $l.Name, $hits, $hitsFolder, $miss, $openFail, $t0, $t1, $crash
    }
    # resumen de la ultima sesion (primer log = el mas reciente)
    $lines += ""
    $lines += "== Ultima sesion ($($logs[0].Name)) =="
    $lastLog = $logs[0].FullName
    $hits = Select-String -LiteralPath $lastLog -Pattern "AFS OVERRIDE HIT.*" | ForEach-Object { $_.Line }
    if ($hits) { $lines += "overrides servidos:"; $lines += $hits } else { $lines += "ningun override servido en la ultima sesion" }
    $lastCrash = Select-String -LiteralPath $lastLog -Pattern "UNHANDLED EXCEPTION|std::terminate" | Select-Object -Last 1
    if ($lastCrash) { $lines += "ULTIMO CRASH: $($lastCrash.Line)" }

    Write-File "logs_classified.txt" ($lines -join "`n")
    $lines | ForEach-Object { Write-Host "  $_" }
}

# ---- main --------------------------------------------------------------------
Write-Host "lab_f0.ps1 - root=$ROOT build=$BuildDir (dryrun=$DryRun)"
Assert-OutDir
if ($Mode -eq "All" -or $Mode -eq "Manifest")    { Invoke-Manifest }
if ($Mode -eq "All" -or $Mode -eq "Inventory")   { Invoke-Inventory }
if ($Mode -eq "All" -or $Mode -eq "ClassifyLogs"){ Invoke-ClassifyLogs }
Write-Host "DONE -> $ABS_OUT"