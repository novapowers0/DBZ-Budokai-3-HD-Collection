# dbz3 - Verificacion de un paquete de release local antes de
# publicar. Comprueba SHA256 del core dual y del runtime SDK baseline en stage
# y ZIP, VERSIONINFO del exe, ausencia de assets del juego y mods/ vacia.
#
# Uso:
#   powershell -ExecutionPolicy Bypass -File tools\verify_release.ps1 [-Stage <path>] [-Version v1.1.1]
#
# Devuelve exit code 0 si todo OK, 1 si hay errores.

param(
[Parameter(Mandatory=$true)][string]$Stage,
[string]$Version = "",
[string]$ProjectRoot = (Split-Path -Parent $PSScriptRoot)
)

$ErrorActionPreference = "Stop"
$root = (Resolve-Path -LiteralPath $ProjectRoot).ProviderPath
# H8 24/10: sin -Version, la version se saca de src\version.rc (DBZ3_VERSION_STR)
if ($Version -eq "") {
    $rcText = Get-Content -Raw -LiteralPath (Join-Path $root "src\version.rc")
    if ($rcText -match 'define\s+DBZ3_VERSION_STR\s+"([0-9.]+)"') { $Version = "v" + $Matches[1] }
}
if ($Version -notmatch '^v\d+\.\d+\.\d+(?:\.\d+)?(?:-[a-zA-Z0-9-]+)?$') { throw "Version no valida: $Version" }

$baseline = Join-Path $root "rexglue-sdk-0.10\out\win-amd64-baseline"
$canonical = [ordered]@{
    "dbz3.exe"               = Join-Path $root "out\build\win-amd64-dual\dbz3.exe"
    "rexruntime.dll"         = Join-Path $baseline "rexruntime.dll"
    "rexgpu-xenos.dll"        = Join-Path $baseline "rexgpu-xenos.dll"
    "amd_fidelityfx_dx12.dll" = Join-Path $baseline "amd_fidelityfx_dx12.dll"
}
$expectedHashes = @{}
$errors = @()

function Get-StreamSha256([System.IO.Stream]$Stream) {
    $sha = [System.Security.Cryptography.SHA256]::Create()
    try { [BitConverter]::ToString($sha.ComputeHash($Stream)).Replace('-', '') }
    finally { $sha.Dispose() }
}

function Get-BinarySha256([string]$Path) {
    $stream = [System.IO.File]::OpenRead($ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($Path))
    try { Get-StreamSha256 $stream }
    finally { $stream.Dispose() }
}

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
# 2026-10-30 v1.5.5.6: rexruntime rebuilt for additive UI blending (Shenron glow, D3D12/Vulkan): 11189760.
# 2026-10-17: menu F1 nuevo (quick_menu.cpp) y volcado de audio dbz3_audio_dump (sdl_audio_driver.cpp) = 11172864 / 6596608.
# 2026-10-16 ronda 4 (frente R15): modelos de la interfaz con el z-buffer del juego (dbz3_ui_depth) = 11158528 / 6596608.
# 2026-11-01 v1.5.6: DLL canonicas SIN cambios (11189760 / 6664192 / 5413888, medidas en el stage D:\tmp\rel156); solo cambia la etiqueta de referencia.
# 2026-11-05 v1.5.6.1: rexruntime/rexgpu-xenos recompiladas con el sello 1.5.6.1 (11228672 / 6688768: + carga de mods indexada, recuperacion de audio, contadores de GPU y apano de viewport para Intel).
# 2026-11-06 v1.5.6.1: rexruntime 11228672 (sha 2dca9fab5579) / rexgpu-xenos 6700544 (sha 53a5853b3bcd): + dbz3_sky_follow (eIW8) y recompilacion del baseline.
# 2026-11-03: baseline recompilado con solar-Z optativo (apagado por defecto).
# 2026-11-23 v1.5.7: DLL canonicas del 22/11 (las de toda la bateria 1.5.7, SHA en copy_sdk_dlls); 13/11: sello 1.5.7; rexruntime 11247616 / rexgpu-xenos 6709760 (12/11: filtro de mods, baja latencia, bandas keep, cielo de noche del Clima).
$canonVersion = "1.5.7"
$canonSizes = @{
    "rexruntime.dll"          = 11254784
    "rexgpu-xenos.dll"        = 6710272
    "amd_fidelityfx_dx12.dll" = 5413888
}

if (-not (Test-Path -LiteralPath $Stage -PathType Container)) {
    Write-Error "No existe el stage: $Stage"
    exit 1
}
$Stage = (Get-Item -LiteralPath $Stage).FullName

# --- identidad de los binarios del stage --------------------------------
foreach ($name in $canonical.Keys) {
    $source = $canonical[$name]
    $staged = Join-Path $Stage $name
    if (-not (Test-Path -LiteralPath $source -PathType Leaf)) {
        $errors += "falta la referencia canonica: $source"
        continue
    }
    $expectedHashes[$name] = Get-BinarySha256 $source
    if (-not (Test-Path -LiteralPath $staged -PathType Leaf)) {
        $errors += "falta $name en el stage"
    } elseif ((Get-BinarySha256 $staged) -ne $expectedHashes[$name]) {
        $errors += "stage: $name no coincide por SHA256 con $source"
    } else {
        Write-Output "stage OK: $name (SHA256 coincide con la referencia)"
    }
}

# --- exe: existe y VERSIONINFO -------------------------------------------
$exe = Join-Path $Stage "dbz3.exe"
if (Test-Path -LiteralPath $exe -PathType Leaf) {
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

# --- tamanos de referencia de las DLLs del runtime ----------------------
foreach ($dll in $canonSizes.Keys) {
    $inStage = Join-Path $Stage $dll
    if (-not (Test-Path -LiteralPath $inStage -PathType Leaf)) { continue }
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
} else {
    $errors += 'Falta version.rc o dbz3_build.h para verificar el sello fuente'
}

# --- partida al 100% de Mods nativos (issue #74: la 1.5.7 salio sin ella) ---
foreach ($f in "data.bin", "DBZ3.header") {
    if (Test-Path -LiteralPath (Join-Path $Stage "mods_nativos\partida_100\$f")) { Write-Output "partida_100 OK: $f" }
    else { $errors += "falta mods_nativos\partida_100\$f (Desbloquear todo no funcionaria)" }
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

# --- zip: identidad de todos los archivos y exclusiones -----------------
$zip = Join-Path (Split-Path -Parent $Stage) "DBZ-Budokai-3-HD-Collection-$Version.zip"
if ($Version -ne "" -and (Test-Path -LiteralPath $zip)) {
    $py = (Get-Command python -ErrorAction Stop).Source
    & $py -B (Join-Path $PSScriptRoot 'release_content.py') check $Stage $zip
    if ($LASTEXITCODE -ne 0) { $errors += 'El inventario o el contenido del ZIP no supera la politica de release' }
    Add-Type -AssemblyName System.IO.Compression.FileSystem
    $z = [System.IO.Compression.ZipFile]::OpenRead($zip)
    try {
        foreach ($name in $canonical.Keys) {
            # Acepta solo una entrada por nombre, en la raiz del ZIP.
            $entries = @($z.Entries | Where-Object { ($_.FullName -split '[/\\]')[-1] -eq $name })
            if ($entries.Count -ne 1 -or $entries[0].FullName -ne $name) {
                $errors += "zip: se esperaba exactamente un $name en la raiz, sin duplicados"
                continue
            }
            if (-not $expectedHashes.ContainsKey($name)) { continue }
            $stream = $entries[0].Open()
            try { $hash = Get-StreamSha256 $stream }
            finally { $stream.Dispose() }
            if ($hash -ne $expectedHashes[$name]) {
                $errors += "zip: $name no coincide por SHA256 con $($canonical[$name])"
            } else {
                Write-Output "zip OK: $name (SHA256 coincide con la referencia)"
            }
        }
        # 1.5.1.2: escenarios de PS2 y Modo Heroe sin Kit ni Python instalados
        foreach ($need in @("python[/\\]python\.exe$", "mod center hd[/\\]campos_iw\.py$", "mod center hd[/\\]escenarios_convertir\.py$",
                            "mod center hd[/\\]iso\.py$", "mod center hd[/\\]importar\.py$", "mod center hd[/\\]cac[/\\]vista_heroe\.py$", "res[/\\]heroe[/\\]sagas\.toml$", "mod center hd[/\\]heroe_sagas_a_du\.py$", "mod center hd[/\\]xv2[/\\]xv2_cac_b3\.py$","mod center hd[/\\]cac[/\\]nombre_levelup\.py$",
                            "awo_tools[/\\]amo2awo\.py$", "awo_tools[/\\]ps2hd\.py$", "python[/\\]Lib[/\\]site-packages[/\\]PIL[/\\]Image\.py$",
                            "python[/\\]Lib[/\\]site-packages[/\\]scipy[/\\]spatial[/\\]__init__\.py$",
                            "mod center hd[/\\]nova_recetas\.py$", "awo_tools[/\\]xeno_a_b3\.py$", "awo_tools[/\\]xeno_fisica\.py$",
                            "awo_tools[/\\]dae_lector\.py$", "awo_tools[/\\]dae_a_b3\.py$", "mod center hd[/\\]tactics_manifest\.toml$",
                            "mod center hd[/\\]tactics_textos\.toml$", "ingame_ui[/\\]i18n_modos\.toml$",
                            "res[/\\]heroe[/\\]piezas_cac\.toml$", "res[/\\]survivors[/\\]survivors\.toml$",
                            "res[/\\]mejoras[/\\]ui[/\\]ui_barra_vida_marco\.png$", "res[/\\]survivors[/\\]03_fondos[/\\]llanura[/\\]fondo_llanura_suelo\.png$",
                            "mods[/\\]README\.md$", "README_PRIMER_ARRANQUE\.txt$", "baserom\.md$", "gamecontrollerdb\.txt$")) {
            if (-not ($z.Entries | Where-Object { $_.FullName -match ('^' + $need) })) { $errors += "el zip no lleva '$need'" }
        }
        if ((Test-Path -LiteralPath (Join-Path $Stage 'nvngx_dlss.dll')) -and
            -not (Test-Path -LiteralPath (Join-Path $Stage 'NVIDIA_DLSS_LICENSE.txt') -PathType Leaf)) {
            $errors += 'Falta la licencia de DLSS'
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
    Write-Output "VERIFICACION ESTATICA OK"
    exit 0
} else {
    Write-Output "ERRORES ($($errors.Count)):"
    $errors | ForEach-Object { Write-Output "  - $_" }
    exit 1
}
