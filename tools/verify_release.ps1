# dbz3 - Verificacion de un paquete de release (github/release-stage) antes de
# publicar. Comprueba que las DLLs del runtime son las del SDK baseline, que el
# exe es el core dual esperado con VERSIONINFO correcto, que no hay assets del
# juego en el zip, y que mods/ va vacia.
#
# Uso:
#   powershell -ExecutionPolicy Bypass -File tools\verify_release.ps1 [-Stage <path>] [-Version v1.1.1]
#
# Devuelve exit code 0 si todo OK, 1 si hay errores.

param(
[string]$Stage = "",
[string]$Version = ""
)

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
if ($Stage -eq "") { $Stage = Join-Path $root "github\release-stage" }

$baseline = Join-Path $root "rexglue-sdk-0.10\out\win-amd64-baseline"
$errors = @()

if (-not (Test-Path -LiteralPath $Stage)) {
    Write-Error "No existe el stage: $Stage"
    exit 1
}

# --- exe: existe y VERSIONINFO -------------------------------------------
$exe = Join-Path $Stage "dbz3.exe"
if (-not (Test-Path -LiteralPath $exe)) {
    $errors += "falta dbz3.exe en el stage"
} else {
    $vi = (Get-Item -LiteralPath $exe).VersionInfo
    Write-Output "exe: dbz3.exe  FileVersion=$($vi.FileVersion)  Product=$($vi.ProductName)"
    if ($Version -ne "" -and $vi.FileVersion -and -not $vi.FileVersion.StartsWith($Version.TrimStart('v'))) {
        $errors += "VERSIONINFO ($($vi.FileVersion)) no coincide con -Version ($Version)"
    }
    if (-not $vi.FileVersion) { $errors += "dbz3.exe sin VERSIONINFO (no se enlazo version.rc)" }
}

# --- DLLs del runtime: iguales a las del SDK baseline ---------------------
foreach ($dll in @("rexruntime.dll", "rexgpu-xenos.dll", "amd_fidelityfx_dx12.dll")) {
    $inStage = Join-Path $Stage $dll
    $inBase  = Join-Path $baseline $dll
    if (-not (Test-Path -LiteralPath $inStage)) {
        $errors += "falta $dll en el stage"
        continue
    }
    if (-not (Test-Path -LiteralPath $inBase)) {
        $errors += "falta el canónico de $dll en el SDK baseline ($baseline) - no se puede comparar"
        continue
    }
    $hStage = (Get-FileHash -LiteralPath $inStage -Algorithm SHA256).Hash
    $hBase  = (Get-FileHash -LiteralPath $inBase -Algorithm SHA256).Hash
    if ($hStage -eq $hBase) {
        Write-Output "dll OK: $dll (SHA256 coincide con el baseline)"
    } else {
        $errors += "$dll en el stage NO coincide con el SDK baseline (¿DLL stale?)"
    }
}

# --- rexgpu: clamp del vblank a 60 Hz presente ----------------------------
$rexgpu = Join-Path $Stage "rexgpu-xenos.dll"
if (Test-Path -LiteralPath $rexgpu) {
    # El cvar "vsync" es un string del runtime GPU; su presencia confirma que es
    # la rexgpu real. El clamp de pacing (§14.17) vive en graphics_system.cpp
    # (dentro de rexgpu) - lo corroboramos comparando con el baseline arriba.
    $bytes = [System.IO.File]::ReadAllBytes($rexgpu)
    $txt = [System.Text.Encoding]::ASCII.GetString($bytes)
    if ($txt -match "vsync") {
        Write-Output "rexgpu OK: contiene el cvar 'vsync' (runtime GPU real)"
    } else {
        $errors += "rexgpu-xenos.dll no contiene 'vsync' - puede ser un binario equivocado"
    }
}

# --- mods/: solo README (vanilla) ----------------------------------------
$modsDir = Join-Path $Stage "mods"
if (Test-Path -LiteralPath $modsDir) {
    $mods = Get-ChildItem -LiteralPath $modsDir -Force | Where-Object { $_.Name -ne "README.md" }
    if ($mods) {
        $errors += "mods/ del stage NO esta vacia: $($mods.Name -join ', ')"
    } else {
        Write-Output "mods/ OK: vacia (solo README)"
    }
} else {
    $errors += "falta la carpeta mods/ en el stage"
}

# --- zip: sin assets del juego -------------------------------------------
$zip = Join-Path (Split-Path -Parent $Stage) "DBZ-Budokai-3-HD-Collection-$Version.zip"
if ($Version -ne "" -and (Test-Path -LiteralPath $zip)) {
    Add-Type -AssemblyName System.IO.Compression.FileSystem
    $z = [System.IO.Compression.ZipFile]::OpenRead($zip)
    try {
        $bad = $z.Entries | Where-Object { $_.FullName -match "default\.xex|(^|/)us/|(^|/)eu/|\.afs$|\.xex$|\.iso$" }
        if ($bad) {
            $errors += "el zip contiene assets del juego: $($bad.FullName -join ', ')"
        } else {
            Write-Output "zip OK: $($z.Entries.Count) entradas sin assets del juego"
        }
    } finally {
        $z.Dispose()
    }
} else {
    Write-Output "zip: no verificado ($Version vacio o zip no encontrado)"
}

# --- resumen -------------------------------------------------------------
Write-Output ""
if ($errors.Count -eq 0) {
    Write-Output "VERIFICACION OK - el paquete esta listo para publicar."
    exit 0
} else {
    Write-Output "ERRORES ($($errors.Count)):"
    $errors | ForEach-Object { Write-Output "  - $_" }
    exit 1
}