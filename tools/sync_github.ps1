# dbz3 - Sincroniza las carpetas versionables del proyecto hacia github/ (el
# repo que se sube a GitHub). github/ NO es un repo git local del trabajo; es
# la copia versionable. Este script replica el proceso de AGENTS.md §9.1.
#
# Uso:
#   powershell -ExecutionPolicy Bypass -File tools\sync_github.ps1 [-DryRun]
#
# Lo que sincroniza (raiz del proyecto -> github/):
#   src/, docs/, awo_tools/, "mod center hd/", tools/ (carpetas versionables)
#   archivos raiz: AGENTS.md, AWO_FORMAT.md, CMakeLists.txt, CMakePresets.json,
#                  dbz3_config.toml, dbz3_manifest.toml
# Lo que NO sincroniza (o se gestiona aparte):
#   - mods/          -> github/mods queda VACIA (solo README; los mods reales
#                       contienen binarios de personajes, no se suben)
#   - patches/       -> se actualiza MANUALMENTE (github/patches/README.md)
#                       cuando se toca el SDK
#   - rexglue-sdk/, rexglue/, generated/, out/, assets del juego -> .gitignore
#
# Reglas del .gitignore de github/ que el script respeta:
#   - tools/xbcompress.exe / xbdecompress.exe se CONSERVAN (excepcion
#     !tools/*.exe); el resto de *.exe/*.dll no entra.

param(
[switch]$DryRun
)

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
$dest = Join-Path $root "github"

if (-not (Test-Path -LiteralPath $dest)) { throw "No existe github/ en $dest" }

function Sync-Tree([string]$src, [string]$rel, [string[]]$ExcludeDir = @(),
                   [string[]]$ExcludeFile = @()) {
    $srcFull = Join-Path $root $src
    $dstFull = Join-Path $dest $rel
    if (-not (Test-Path -LiteralPath $srcFull)) {
        Write-Warning "origen no existe, se omite: $src"
        return
    }
    if ($DryRun) {
        Write-Output "[dry] robocopy /MIR $srcFull -> $dstFull"
        return
    }
    New-Item -ItemType Directory -Path $dstFull -Force | Out-Null
    $args = @("$srcFull", "$dstFull", "/MIR", "/NJH", "/NJS", "/NDL", "/NP")
    foreach ($x in $ExcludeDir) { $args += "/XD"; $args += $x }
    foreach ($x in $ExcludeFile) { $args += "/XF"; $args += $x }
    & robocopy @args | Out-Null
    if ($LASTEXITCODE -ge 8) { throw "robocopy fallo para $src (codigo $LASTEXITCODE)" }
    Write-Output "sincronizado: $rel/"
}

Write-Output "=== Sincronizando proyecto -> github/ ==="

# Carpetas versionables (mirror exacto). Excluye cachés/artefactos que el
# .gitignore de github/ ya ignora.
Sync-Tree "src"                  "src"
Sync-Tree "docs"                 "docs"
Sync-Tree "awo_tools"            "awo_tools"      @("bins_trabajo")
Sync-Tree "mod center hd"        "mod center hd"  @("__pycache__")
# tools: mirror pero CONSERVA los .exe canonicos (xbcompress/xbdecompress) que
# viven solo en github/ (ver .gitignore !tools/*.exe).
Sync-Tree "tools" "tools" @() @("xbcompress.exe", "xbdecompress.exe")

if (-not $DryRun) {
    # Conservar los .exe canonicos del repo (ver .gitignore !tools/*.exe).
    foreach ($t in @("xbcompress.exe", "xbdecompress.exe")) {
        $canon = Join-Path $dest "tools\$t"
        if (-not (Test-Path -LiteralPath $canon)) {
            $srcExe = Join-Path $root "tools\$t"
            if (Test-Path -LiteralPath $srcExe) {
                Copy-Item -LiteralPath $srcExe $canon
                Write-Output "tools: conservado $t"
            }
        }
    }
}

# Archivos raiz versionables.
$rootFiles = @("AGENTS.md", "AWO_FORMAT.md", "CMakeLists.txt", "CMakePresets.json",
               "dbz3_config.toml", "dbz3_manifest.toml")
foreach ($f in $rootFiles) {
    $srcFile = Join-Path $root $f
    if (-not (Test-Path -LiteralPath $srcFile)) {
        Write-Warning "raiz no existe, se omite: $f"
        continue
    }
    if ($DryRun) {
        Write-Output "[dry] copiar $f"
        continue
    }
    Copy-Item -LiteralPath $srcFile (Join-Path $dest $f) -Force
    Write-Output "raiz: $f"
}

if ($DryRun) {
    Write-Output "=== Dry run - nada copiado ==="
    return
}

Write-Output ""
Write-Output "=== Estado git de github/ (revisar antes de commitear) ==="
Push-Location $dest
try {
    git status --short
} finally {
    Pop-Location
}

Write-Output ""
Write-Output "Recordatorio:"
Write-Output "  - patches/ del SDK se actualiza MANUALMENTE (github/patches/README.md)"
Write-Output "  - mods/ se mantiene vacia en github/ (los mods reales no se suben)"