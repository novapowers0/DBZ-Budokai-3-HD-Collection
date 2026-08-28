# DBZ Budokai 3 HD Collection - release packaging script
# Assembles the standalone release as a SINGLE universal folder: one dbz3.exe
# (dual-region core: US/NA + EU/PAL, auto-detects the default.xex) and ONE set
# of runtime DLLs compiled at the BASELINE x86-64 ISA (SSSE3, Core 2 2006+),
# so the same package runs on any x86-64 CPU - no variant folders, no bootstrap.
#
# Usage:
#   powershell -ExecutionPolicy Bypass -File tools\make_release.ps1 [-Version v1.1.0] [-OutDir <path>]
#
# Layout produced:
#   <stage>/
#     dbz3.exe                    <- dual-region universal core (the ONLY exe)
#     rexruntime.dll, rexgpu-xenos.dll, amd_fidelityfx_dx12.dll, amd_fidelityfx_vk.dll,
#     TracyClient.dll, SPIRV-Tools-shared.dll, MSVC CRT DLLs
#     mod center hd/              <- modding toolkit (scripts + XDK tools)
#     mods/                       <- empty, with README
#     README_PRIMER_ARRANQUE.txt, MODDING_README.md, RELEASE_README.md, baserom.md
#   <version>.zip
#
# ⚠️ AV / UPX: do NOT pass -UpxPath for distributed releases. UPX-packed
# executables are a well-known malware packaging pattern and get flagged as
# false positives by many antivirus products (users reported v1.0.6 as "virus").
# -UpxPath is kept only for local testing.

param(
[string]$Version = "v1.1.0",
[string]$OutDir = "",
[string]$UpxPath = ""
)

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot

$build   = Join-Path $root "out\build\win-amd64-dual"
$sdk     = Join-Path $root "rexglue-sdk-0.10\out\win-amd64-baseline"
$modcenter = Join-Path $root "mod center hd"

if ($OutDir -eq "") { $OutDir = Join-Path $root "github\release-stage" }

# --- inputs -------------------------------------------------------------
$core        = Join-Path $build "dbz3.exe"
$runtime_dlls = @("rexruntime.dll","rexgpu-xenos.dll","amd_fidelityfx_dx12.dll")
$shared_dlls = @("amd_fidelityfx_vk.dll","SPIRV-Tools-shared.dll","TracyClient.dll",
"msvcp140.dll","msvcp140_atomic_wait.dll",
"vcruntime140.dll","vcruntime140_1.dll")

if (-not (Test-Path -LiteralPath $core)) { throw "Falta $core" }
if (-not (Test-Path -LiteralPath $sdk))  { throw "Falta el SDK baseline en $sdk" }
foreach ($dll in $runtime_dlls) {
if (-not (Test-Path -LiteralPath (Join-Path $sdk $dll))) { throw "Falta $dll en $sdk" }
}

# Snapshot of the shared DLLs + docs from the previous release-stage (the
# MSVC CRT DLLs are not produced by the build; the current release carries
# the canonical, already-proven copies).
$old_stage = $OutDir
$snap_dir  = Join-Path $env:TEMP "dbz3_release_snapshot"
if (Test-Path -LiteralPath $snap_dir) { Remove-Item -LiteralPath $snap_dir -Recurse -Force }
if (Test-Path -LiteralPath $old_stage) {
    New-Item -ItemType Directory -Path $snap_dir | Out-Null
    foreach ($dll in $shared_dlls) {
        $from = Join-Path $old_stage $dll
        if (Test-Path -LiteralPath $from) { Copy-Item -LiteralPath $from (Join-Path $snap_dir $dll) }
    }
    foreach ($f in @("baserom.md","MODDING_README.md","RELEASE_README.md","README_PRIMER_ARRANQUE.txt")) {
        $from = Join-Path $old_stage $f
        if (Test-Path -LiteralPath $from) { Copy-Item -LiteralPath $from (Join-Path $snap_dir $f) }
    }
}

# --- stage --------------------------------------------------------------
if (Test-Path -LiteralPath $OutDir) {
    Remove-Item -LiteralPath $OutDir -Recurse -Force
}
New-Item -ItemType Directory -Path $OutDir | Out-Null

# The single universal exe
Copy-Item -LiteralPath $core (Join-Path $OutDir "dbz3.exe")
if ($UpxPath -ne "") {
    & $UpxPath -9 -q (Join-Path $OutDir "dbz3.exe")
}

# Baseline runtime DLLs (from the SDK build dir)
foreach ($dll in $runtime_dlls) {
    Copy-Item -LiteralPath (Join-Path $sdk $dll) (Join-Path $OutDir $dll)
}

# Shared DLLs: canonical copy in github/ root (versioned), then the snapshot
# of the previous release, then the US build dir.
foreach ($dll in $shared_dlls) {
    $from = Join-Path $root "github\$dll"
    if (-not (Test-Path -LiteralPath $from)) { $from = Join-Path $snap_dir $dll }
    if (-not (Test-Path -LiteralPath $from)) { $from = Join-Path $root "out\build\win-amd64-release\$dll" }
    if (Test-Path -LiteralPath $from) {
        Copy-Item -LiteralPath $from (Join-Path $OutDir $dll)
    } else {
        Write-Warning "No se encontro '$dll' - omitido"
    }
}

# Modding toolkit (runtime subset)
if (Test-Path -LiteralPath $modcenter) {
    $mcd = Join-Path $OutDir "mod center hd"
    New-Item -ItemType Directory -Path $mcd | Out-Null
    foreach ($f in @("catalog_b3.cat","swap_b3.py","texture_b3.py")) {
        if (Test-Path -LiteralPath (Join-Path $modcenter $f)) {
            Copy-Item -LiteralPath (Join-Path $modcenter $f) (Join-Path $mcd $f)
        }
    }
    if (Test-Path -LiteralPath (Join-Path $modcenter "tools")) {
        Copy-Item -LiteralPath (Join-Path $modcenter "tools") (Join-Path $mcd "tools") -Recurse
    }
}

# XDK compression tools (xbcompress/xbdecompress) needed by the mod pipeline.
# Canonical copy in github/tools/, plus the XDK runtime DLLs that the old XDK
# binaries require (MSVCR71/MSVCP71/xbdm) from the XDK folder of the repo.
$toolkit_dir = Join-Path $OutDir "mod center hd\tools"
New-Item -ItemType Directory -Path $toolkit_dir -Force | Out-Null
foreach ($t in @("xbcompress.exe","xbdecompress.exe")) {
    $from = Join-Path $root "github\tools\$t"
    if (Test-Path -LiteralPath $from) {
        Copy-Item -LiteralPath $from (Join-Path $toolkit_dir $t)
    }
}
$xdksrc = Get-ChildItem (Join-Path $root "mod center") -Directory -Filter "*Compression*" |
    Select-Object -First 1
if ($xdksrc) {
    foreach ($t in @("MSVCR71.dll","MSVCP71.dll","xbdm.dll")) {
        $from = Join-Path $xdksrc.FullName $t
        if (Test-Path -LiteralPath $from) {
            Copy-Item -LiteralPath $from (Join-Path $toolkit_dir $t)
        }
    }
}

# Mods folder (empty, documented)
$mods_dir = Join-Path $OutDir "mods"
New-Item -ItemType Directory -Path $mods_dir | Out-Null
Set-Content -LiteralPath (Join-Path $mods_dir "README.md") -Value (
    "Coloca aqui tus mods. Cada mod en su carpeta: mods/<nombre>/ con manifest.txt.`n" +
    "Mas informacion en MODDING_README.md y en la pestana Mods del launcher.")

# Docs (priority: current sources in github/, then the snapshot of the
# previous release, then whatever is already staged).
foreach ($f in @("baserom.md","MODDING_README.md","RELEASE_README.md")) {
    $from = Join-Path $root "github\$f"
    if (-not (Test-Path -LiteralPath $from)) { $from = Join-Path $snap_dir $f }
    if (-not (Test-Path -LiteralPath $from)) { $from = Join-Path $OutDir $f }
    if (Test-Path -LiteralPath $from) { Copy-Item -LiteralPath $from (Join-Path $OutDir $f) }
}
$primer = Join-Path $root "github\README_PRIMER_ARRANQUE.txt"
if (Test-Path -LiteralPath $primer) {
    Copy-Item -LiteralPath $primer (Join-Path $OutDir "README_PRIMER_ARRANQUE.txt")
}

# --- zip ----------------------------------------------------------------
$zip = Join-Path (Split-Path -Parent $OutDir) "DBZ-Budokai-3-HD-Collection-$Version.zip"
if (Test-Path -LiteralPath $zip) { Remove-Item -LiteralPath $zip -Force }
Compress-Archive -Path (Join-Path $OutDir "*") -DestinationPath $zip -CompressionLevel Optimal

Write-Output "Release '$Version' montada en:"
Write-Output "  $OutDir"
Write-Output "  $zip ($((Get-Item $zip).Length) B)"