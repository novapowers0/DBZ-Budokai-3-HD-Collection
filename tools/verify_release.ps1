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
# H8 24/10: sin -Version, la version se saca de src\version.rc (DBZ3_VERSION_STR)
if ($Version -eq "") {
    $rcText = Get-Content -Raw -LiteralPath (Join-Path $root "src\version.rc")
    if ($rcText -match 'define\s+DBZ3_VERSION_STR\s+"([0-9.]+)"') { $Version = "v" + $Matches[1] }
}

$baseline = Join-Path $root "rexglue-sdk-0.10\out\win-amd64-baseline"
$errors = @()

# Tamanos de las DLL canonicas de la release actual (SDK rama dbz3-burstlimit,
# 2026-10-05). Historico: v1.3.0 = 10920448 / 6360064, v1.4.0 = 11034624 / 6372864,
# v1.4.3 = 11109888 / 6431232.
# v1.4.3.1 = 11127296 / 6448640. v1.4.4.5 (publicada) = 11129344 / 6482432.
# 1.4.4.5 + Fase 0 SDK (B29/MODS-ROOT/MAPPINGS/VFS) + packs xN, 2026-10-13.
# amd_fidelityfx_dx12.dll no cambia desde 2026-08-28.
# v1.4.5 = 11145216 / 6537216. v1.4.5.1 = indice de packs que se rehace al cambiar la lista.
# v1.4.5.2 = 11145216 / 6540288. 2026-10-15 ronda 2: SSAO opcional dbz3_relight (frente grafico) = 6589440. v1.4.6 = 11145216 / 6589440.
# 2026-10-15 ronda 3 (frente C): tiras doradas nativas ocultables (dbz3_ui_side_borders) = 6594048.
# 2026-10-23 v1.5.5 (version stamp update): 11189248 / 6658560; same canonical SDK DLL sizes.
# 2026-10-24 v1.5.5.3: rexgpu-xenos rebuilt by PACK (pack update/ skip + dynamic re-hash limit, pack SRV fix): 6664192.
# 2026-10-17: menu F1 nuevo (quick_menu.cpp) y volcado de audio dbz3_audio_dump (sdl_audio_driver.cpp) = 11172864 / 6596608.
# 2026-10-16 ronda 4 (frente R15): modelos de la interfaz con el z-buffer del juego (dbz3_ui_depth) = 11158528 / 6596608.
$canonVersion = "1.5.5"
$canonSizes = @{
    "rexruntime.dll"          = 11189248
    "rexgpu-xenos.dll"        = 6664192
    "amd_fidelityfx_dx12.dll" = 5413888
}

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
    # Compare against the NUMERIC part of -Version, so release names with a
    # suffix (-EX, -clasico) still verify: the VERSIONINFO carries the extra
    # build number (v1.2.4-EX -> v1.2.4.1), so "v1.2.4-EX" must match "1.2.4.1".
    $base_ver = if ($Version -ne "") {
        ([regex]::Match($Version.TrimStart('v'), '^[0-9]+(\.[0-9]+)*')).Value
    } else { "" }
    $versionMatch = $true
    if ($Version -ne "" -and $base_ver -eq "") { $versionMatch = $false }
    if ($base_ver -ne "" -and $vi.FileVersion) {
        $want = @($base_ver.Split('.'))
        $have = @($vi.FileVersion.Split('.'))
        if ($want.Count -gt $have.Count) { $versionMatch = $false }
        else {
            for ($i = 0; $i -lt $want.Count; ++$i) {
                if ([int]$want[$i] -ne [int]$have[$i]) { $versionMatch = $false }
            }
        }
    }
    if (-not $versionMatch) {
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
    # Tamano de referencia de la release actual: atrapa un baseline sobrescrito por
    # otro build del SDK (p. ej. sin los cambios de la rama dbz3-burstlimit). Al
    # recompilar el SDK para una release nueva hay que actualizar esta tabla.
    if ($canonSizes.ContainsKey($dll)) {
        $len = (Get-Item -LiteralPath $inStage).Length
        if ($len -eq $canonSizes[$dll]) {
            Write-Output "dll OK: $dll ($len B = referencia v$canonVersion)"
        } else {
            $errors += "$dll mide $len B; la referencia v$canonVersion es $($canonSizes[$dll]) B (¿DLL de otro build del SDK?)"
        }
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

# --- sello de version del runtime (deteccion de instalaciones mixtas) -----
# Las DLLs no llevan VERSIONINFO: publican su build por cvar (dbz3_runtime_build /
# dbz3_gpu_build) desde rex/dbz3_build.h, y el launcher avisa al usuario cuando no
# coincide con la del exe. Si el sello se queda atras, ese aviso salta en TODAS
# las instalaciones nuevas: hay que subirlo junto con version.rc.
$rc = Join-Path $root "src\version.rc"
$buildh = Join-Path $root "rexglue-sdk-0.10\include\rex\dbz3_build.h"
if ((Test-Path -LiteralPath $rc) -and (Test-Path -LiteralPath $buildh)) {
    $rcVer = ([regex]::Match((Get-Content -LiteralPath $rc -Raw), 'DBZ3_VERSION_STR\s+"([^"]+)"')).Groups[1].Value
    $hVer = ([regex]::Match((Get-Content -LiteralPath $buildh -Raw), 'DBZ3_RUNTIME_BUILD\s+"([^"]+)"')).Groups[1].Value
    if ($rcVer -eq "" -or $hVer -eq "") {
        $errors += "no se pudo leer la version de version.rc o el sello de dbz3_build.h"
    } elseif ((($rcVer -split '[.]')[0..2] -join '.') -ne (($hVer -split '[.]')[0..2] -join '.')) {   # 1.5.1.1 = repack de 1.5.1
        $errors += "sello del runtime ($hVer en rex/dbz3_build.h) != src/version.rc ($rcVer)"
    } else {
        Write-Output "sello runtime OK: $hVer (coincide con src/version.rc)"
    }
    foreach ($dll in @("rexruntime.dll", "rexgpu-xenos.dll")) {
        $p = Join-Path $Stage $dll
        if (-not (Test-Path -LiteralPath $p)) { continue }
        $txt = [System.Text.Encoding]::ASCII.GetString([System.IO.File]::ReadAllBytes($p))
        $needle = if ($dll -eq "rexruntime.dll") { "Build de rexruntime" } else { "Build de rexgpu" }
        if ($txt -notmatch $needle) {
            $errors += "$dll no contiene el sello de version ($needle): la deteccion de instalacion mixta no lo vera"
        }
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
        if (-not ($z.Entries | Where-Object { $_.FullName -match "mods_nativos[\\/]partida_100[\\/]data\.bin$" })) {
            $errors += "el zip no lleva mods_nativos/partida_100 (la partida al 100% saldria en gris)"
        }
        # 1.5.1.2: escenarios de PS2 y Modo Heroe sin Kit ni Python instalados
        foreach ($need in @("python[/\\]python\.exe$", "mod center hd[/\\]campos_iw\.py$", "mod center hd[/\\]escenarios_convertir\.py$",
                            "mod center hd[/\\]iso\.py$", "mod center hd[/\\]importar\.py$", "mod center hd[/\\]cac[/\\]vista_heroe\.py$",
                            "awo_tools[/\\]amo2awo\.py$", "awo_tools[/\\]ps2hd\.py$", "python[/\\]Lib[/\\]site-packages[/\\]PIL[/\\]Image\.py$")) {
            if (-not ($z.Entries | Where-Object { $_.FullName -match $need })) { $errors += "el zip no lleva '$need' (escenarios de PS2 / Modo Heroe sin Kit)" }
        }
        # Residuos de ejecucion: una carpeta de datos/user_data/logs/iso_cache
        # delataria que se empaqueto la carpeta con la que se jugo, no una limpia.
        $runtime_residue = $z.Entries | Where-Object {
            $_.FullName -match "user_data/|logs/|iso_cache/|dbz3_user\.toml$"
        }
        if ($runtime_residue) {
            $errors += "el zip contiene residuos de ejecucion (user_data/ logs/ iso_cache/ dbz3_user.toml): $($runtime_residue.FullName -join ', ')"
        } else {
            Write-Output "zip OK: sin residuos de ejecucion"
        }
    } finally {
        $z.Dispose()
    }
} else {
    # H8 24/10: sin zip revisado no se puede dar el paquete por bueno
    Write-Output "zip: no verificado ($Version vacio o zip no encontrado: $zip)"
    $errors += "zip no verificado: no se ha revisado $zip"
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
