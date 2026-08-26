# DBZ Budokai 3 HD Collection - release packaging script
# Assembles the standalone release with the ISA bootstrap + the dual-region
# core. The core (dbz3_core.exe) is a SINGLE dual-region binary (US/NA and
# EU/PAL recompilations linked together); the bootstrap only picks the CPU
# variant (dbz3_avx2/ for x86-64-v3 CPUs, dbz3_legacy/ for older ones).
#
# Usage:
#   powershell -ExecutionPolicy Bypass -File tools\make_release.ps1 [-Version v1.0.7] [-OutDir <path>] [-UpxPath <path\to\upx.exe>]
#
# Layout produced:
#   <stage>/
#     dbz3.exe                    <- ISA bootstrap (baseline x86-64)
#     dbz3_avx2/dbz3_core.exe + v3 runtime DLLs   (dual core, AVX2 CPUs)
#     dbz3_legacy/dbz3_core.exe + v2 runtime DLLs (dual core, older CPUs)
#     mod center hd/              <- modding toolkit (scripts + XDK tools)
#     mods/                       <- empty, with README
#     README_PRIMER_ARRANQUE.txt, MODDING_README.md, RELEASE_README.md, baserom.md
#   <version>.zip
#
# With -UpxPath, the core is compressed with UPX before staging (verified clean
# against Windows Defender; see AGENTS.md).

param(
[string]$Version = "v1.0.7",
[string]$OutDir = "",
[string]$UpxPath = ""
)

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot

$build   = Join-Path $root "out\build\win-amd64-dual"
$sdk_v3  = Join-Path $root "rexglue-sdk-0.10\out\win-amd64"
$sdk_v2  = Join-Path $root "rexglue-sdk-0.10\out\win-amd64-legacy"
$modcenter = Join-Path $root "mod center hd"

if ($OutDir -eq "") { $OutDir = Join-Path $root "github\release-stage" }

# --- inputs -------------------------------------------------------------
$bootstrap   = Join-Path $root "out\build\win-amd64-release\dbz3_bootstrap.exe"
$core        = Join-Path $build "dbz3.exe"
$runtime_dlls = @("rexruntime.dll","rexgpu-xenos.dll","amd_fidelityfx_dx12.dll")
$shared_dlls = @("amd_fidelityfx_vk.dll","SPIRV-Tools-shared.dll","TracyClient.dll",
"msvcp140.dll","msvcp140_atomic_wait.dll",
"vcruntime140.dll","vcruntime140_1.dll")

foreach ($p in @($bootstrap, $core)) {
if (-not (Test-Path -LiteralPath $p)) { throw "Falta $p" }
}
foreach ($dll in $runtime_dlls) {
if (-not (Test-Path -LiteralPath (Join-Path $sdk_v3 $dll))) { throw "Falta v3: $dll" }
if (-not (Test-Path -LiteralPath (Join-Path $sdk_v2 $dll))) { throw "Falta v2: $dll" }
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

# Sanity: the two runtime variants must differ in size (v3 vs v2 ISA).
foreach ($dll in $runtime_dlls) {
$s3 = (Get-Item (Join-Path $sdk_v3 $dll)).Length
$s2 = (Get-Item (Join-Path $sdk_v2 $dll)).Length
if ($s3 -eq $s2) { throw "Warning: $dll v3 y v2 tienen el mismo tamaño ($s3) - revisar" }
}

# --- stage --------------------------------------------------------------
if (Test-Path -LiteralPath $OutDir) {
    Remove-Item -LiteralPath $OutDir -Recurse -Force
}
New-Item -ItemType Directory -Path $OutDir | Out-Null

# Bootstrap at the root: dbz3.exe
Copy-Item -LiteralPath $bootstrap (Join-Path $OutDir "dbz3.exe")

$variants = @(
    @{ Name = "dbz3_avx2";    Src = $sdk_v3 },
    @{ Name = "dbz3_legacy";  Src = $sdk_v2 }
)
foreach ($v in $variants) {
    $vdir = Join-Path $OutDir $v.Name
    New-Item -ItemType Directory -Path $vdir | Out-Null
    Copy-Item -LiteralPath $core (Join-Path $vdir "dbz3_core.exe")
    if ($UpxPath -ne "") {
        & $UpxPath -9 -q (Join-Path $vdir "dbz3_core.exe")
    }
    foreach ($dll in $runtime_dlls) {
        Copy-Item -LiteralPath (Join-Path $v.Src $dll) (Join-Path $vdir $dll)
    }
    foreach ($dll in $shared_dlls) {
        # Canonical copy: github/ root (versioned, e.g. the MSVC CRT DLLs),
        # then the snapshot of the previous release, then the US build dir.
        $from = Join-Path $root "github\$dll"
        if (-not (Test-Path -LiteralPath $from)) { $from = Join-Path $snap_dir $dll }
        if (-not (Test-Path -LiteralPath $from)) { $from = Join-Path $root "out\build\win-amd64-release\$dll" }
        if (Test-Path -LiteralPath $from) {
            Copy-Item -LiteralPath $from (Join-Path $vdir $dll)
        } else {
            Write-Warning "No se encontro '$dll' para $($v.Name) - omitido"
        }
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