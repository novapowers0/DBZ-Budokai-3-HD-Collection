# DBZ Budokai 3 HD Collection - release packaging script
# Assembles the standalone release as a SINGLE universal folder: one dbz3.exe
# (dual-region core: US/NA + EU/PAL, auto-detects the default.xex) and ONE set
# of runtime DLLs compiled at the BASELINE x86-64 ISA (SSSE3, Core 2 2006+),
# so the same package runs on any x86-64 CPU - no variant folders, no bootstrap.
#
# Usage:
#   powershell -ExecutionPolicy Bypass -File tools\make_release.ps1 [-Version v1.1.0] [-OutDir <path>]
#
# The version defaults to the one in src/version.rc (single source of truth);
# pass -Version to override (e.g. a "-clasico" suffix).
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
[string]$Version = "",
[string]$OutDir = "",
[string]$UpxPath = ""
)

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot

# Single source of truth for the version: src/version.rc (VERSION_MAJOR/MINOR/
# PATCH/BUILD). Bump it there and the release zip/name follows. VERSION_BUILD
# distinto de 0 se anade como 4o componente (v1.2.8.1, seguimiento de la 1.2.8);
# con 0 se queda en 3 (v1.2.8).
if ($Version -eq "") {
    $rc = Get-Content -LiteralPath (Join-Path $root "src\version.rc")
    $maj = ($rc | Select-String 'VERSION_MAJOR\s+(\d+)' | ForEach-Object { $_.Matches[0].Groups[1].Value } | Select-Object -First 1)
    $min = ($rc | Select-String 'VERSION_MINOR\s+(\d+)' | ForEach-Object { $_.Matches[0].Groups[1].Value } | Select-Object -First 1)
    $pat = ($rc | Select-String 'VERSION_PATCH\s+(\d+)' | ForEach-Object { $_.Matches[0].Groups[1].Value } | Select-Object -First 1)
    $bld = ($rc | Select-String 'VERSION_BUILD\s+(\d+)' | ForEach-Object { $_.Matches[0].Groups[1].Value } | Select-Object -First 1)
    if (-not $maj -or -not $min -or -not $pat) {
        throw "No se pudo leer la version de src\version.rc"
    }
    $Version = "v$maj.$min.$pat"
    if ($bld -and [int]$bld -ne 0) {
        $Version = "$Version.$bld"
    }
}
Write-Output "Version de release: $Version"

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
if (-not (Test-Path -LiteralPath $old_stage)) {
    # The previous stage may have been archived to keep the repo root tidy
    # (github/_archive/release-stage). Use it as the snapshot source so the
    # shared DLLs/docs are carried over anyway.
    $archived_stage = Join-Path $root "github\_archive\release-stage"
    if (Test-Path -LiteralPath $archived_stage) { $old_stage = $archived_stage }
}
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

# SDL game controller mappings (community GameControllerDB, zlib): shipped next
# to dbz3.exe so the runtime's `hid_mappings_file` default resolves and the SDL
# input backend has real mappings for generic pads.
$ctrldb = Join-Path $root "gamecontrollerdb.txt"
if (-not (Test-Path -LiteralPath $ctrldb)) { $ctrldb = Join-Path $root "github\gamecontrollerdb.txt" }
if (Test-Path -LiteralPath $ctrldb) {
    Copy-Item -LiteralPath $ctrldb (Join-Path $OutDir "gamecontrollerdb.txt")
} else {
    Write-Warning "No se encontro gamecontrollerdb.txt - omitido"
}

# Nova's Mods: clave de solo lectura del repo de mods (tools/novas_mods.py clave). Sin ella, la
# pestana del launcher dice que llega en la proxima actualizacion.
$novaKey = Join-Path $root "novas_mods.key"
if (Test-Path -LiteralPath $novaKey) {
    Copy-Item -LiteralPath $novaKey (Join-Path $OutDir "novas_mods.key")
} else {
    Write-Warning "Sin novas_mods.key: Nova's Mods no podra descargar"
}

# Partida al 100% (Mods nativos): sin mods_nativos/partida_100 el boton Aplicar sale en gris
# (Save100Available); hasta la 1.4.4.4 no iba en el paquete (avisos de jugadores, 2026-10-12)
$save100 = Join-Path $root "out\build\win-amd64-release\mods_nativos\partida_100"
if ((Test-Path -LiteralPath (Join-Path $save100 "data.bin")) -and (Test-Path -LiteralPath (Join-Path $save100 "DBZ3.header"))) {
    $dst100 = Join-Path $OutDir "mods_nativos\partida_100"
    New-Item -ItemType Directory -Force -Path $dst100 | Out-Null
    Copy-Item -LiteralPath (Join-Path $save100 "data.bin"), (Join-Path $save100 "DBZ3.header") $dst100
} else {
    Write-Warning "Sin mods_nativos/partida_100: la partida al 100% no se podra aplicar"
}

# 1.4.3 EX: cache inicial de shaders/pipelines (la del build de desarrollo, que ya
# ha visto todo el juego). dbz3.exe la copia a user_data solo si falta: el primer
# combate de una instalacion nueva no compila nada sobre la marcha.
$shader_src = Join-Path $root "out\build\win-amd64-release\user_data\dbz3\cache\shaders\shareable"
$shader_files = @(Get-ChildItem -LiteralPath $shader_src -Filter "4E4D0856.*" -File -ErrorAction SilentlyContinue)
if ($shader_files.Count -gt 0) {
    $shader_dst = Join-Path $OutDir "shader_cache"
    New-Item -ItemType Directory -Path $shader_dst | Out-Null
    $shader_files | ForEach-Object { Copy-Item -LiteralPath $_.FullName (Join-Path $shader_dst $_.Name) }
} else {
    Write-Warning "No hay cache de shaders en $shader_src - omitida"
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

# libjuice (MPL-2.0): su licencia va en la release en cuanto se enlace (DBZ3_LIBJUICE; hoy apagado). Se copia siempre: no pesa nada.
$juice_lic = Join-Path $root "third_party\libjuice\LICENSE"
if (Test-Path -LiteralPath $juice_lic) { Copy-Item -LiteralPath $juice_lic (Join-Path $OutDir "LIBJUICE_LICENSE_MPL-2.0.txt") }

# NVIDIA DLSS (1.4.2): DLL redistribuible del SDK oficial + su licencia (no se sube el SDK).
$dlss_sdk = "D:\SDKs\DLSS"
$dlss = Join-Path $root "out\build\win-amd64-release\nvngx_dlss.dll"
if (Test-Path -LiteralPath $dlss) {
    Copy-Item -LiteralPath $dlss (Join-Path $OutDir "nvngx_dlss.dll")
    $lic = Join-Path $dlss_sdk "LICENSE.txt"
    if (Test-Path -LiteralPath $lic) { Copy-Item -LiteralPath $lic (Join-Path $OutDir "NVIDIA_DLSS_LICENSE.txt") }
    else { Write-Warning "Falta la licencia de DLSS en $dlss_sdk" }
} else {
    Write-Warning "Falta nvngx_dlss.dll: ejecuta tools\copy_sdk_dlls.ps1 (DLSS caera a FSR 3)"
}

# UI propio (TAG TEAM, ONLINE, HUB): incluir recursos diseñados para el port, pero no retratos de personajes
# extraidos del juego; esos anfitriones se leen de los AFS del jugador en src/ingame/native_ui.cpp.
$ingame_ui = Join-Path $root "res\ingame_ui"
if (Test-Path -LiteralPath $ingame_ui) {
    $ingame_dst = Join-Path $OutDir "ingame_ui"
    $game_art = @("hub\anfitrion_bulma.png", "hub\anfitrion_goku.png", "hub\anfitrion_heroe.png",
                  "hub\anfitrion_trunks.png", "hub\anfitrion_vegeta.png", "hub\anfitrion_whis.png")
    Get-ChildItem -LiteralPath $ingame_ui -File -Recurse | ForEach-Object {
        $relative = $_.FullName.Substring($ingame_ui.Length).TrimStart('\')
        if ($game_art -contains $relative) {
            Write-Output "UI: retrato del juego omitido: $relative"
        } else {
            $target = Join-Path $ingame_dst $relative
            New-Item -ItemType Directory -Path (Split-Path -Parent $target) -Force | Out-Null
            Copy-Item -LiteralPath $_.FullName -Destination $target
        }
    }
} else {
    Write-Warning "Falta res\ingame_ui: ejecuta tools\ingame_ui.py (las pantallas propias usaran sustitutos)"
}

# Modo Heroe: sagas y catalogo real de piezas CaC (cuerpo/cara/pelo/ropa/color/voz/aura).
# CMake ya copia res\heroe al build; el stage de release tambien tiene que llevarlo.
$heroe_res = Join-Path $root "res\heroe"
if (Test-Path -LiteralPath $heroe_res) {
    $heroe_dst = Join-Path $OutDir "res\heroe"
    New-Item -ItemType Directory -Path $heroe_dst -Force | Out-Null
    Copy-Item -Path (Join-Path $heroe_res "*") -Destination $heroe_dst -Recurse -Force   # -LiteralPath no expande "*": la 1.5.5 salio sin resheroe
} else {
    Write-Warning "Falta res\heroe: el Modo Heroe usara las sagas de codigo y el catalogo de respaldo"
}

# Datos medidos del original (src/datos.cpp lee res\datos junto al exe). xv2_cac.toml sale de la copia de XV2 del
# jugador (no distribuir): se regenera en su PC.
$datos_res = Join-Path $root "res\datos"
if (Test-Path -LiteralPath $datos_res) {
    $datos_dst = Join-Path $OutDir "res\datos"
    New-Item -ItemType Directory -Path $datos_dst -Force | Out-Null
    Get-ChildItem -LiteralPath $datos_res -File | Where-Object { $_.Name -ne "xv2_cac.toml" } |
        Copy-Item -Destination $datos_dst -Force
}

# Modding toolkit (runtime subset)
if (Test-Path -LiteralPath $modcenter) {
    $mcd = Join-Path $OutDir "mod center hd"
    New-Item -ItemType Directory -Path $mcd | Out-Null
    foreach ($f in @("catalog_b3.cat","swap_b3.py","texture_b3.py","texture_pack.py")) {
        if (Test-Path -LiteralPath (Join-Path $modcenter $f)) {
            Copy-Item -LiteralPath (Join-Path $modcenter $f) (Join-Path $mcd $f)
        }
    }
    # Texture-pack helper: converts the dev texture dump (DDS) to PNG and
    # organizes it by character using the AFS #AZT hashes.
    $importer = Join-Path $root "awo_tools\texture_dump_import.py"
    if (Test-Path -LiteralPath $importer) {
        Copy-Item -LiteralPath $importer (Join-Path $mcd "texture_dump_import.py")
    }
    if (Test-Path -LiteralPath (Join-Path $modcenter "tools")) {
        Copy-Item -LiteralPath (Join-Path $modcenter "tools") (Join-Path $mcd "tools") -Recurse
    }
    # Modo Heroe (vista previa 3D y traje de combate con colores, logo y peinado UT): vista_heroe.py y lo que importa
    # (studies 8 INFORME F2.5; las piezas de pelo UT NO se publican: salen del UT del jugador, cac_piezas\pelo\).
    # Sin Python en el PC del jugador la pantalla sigue con el retrato.
    $cacd = Join-Path $mcd "cac"
    New-Item -ItemType Directory -Path $cacd -Force | Out-Null
    $vh = Join-Path $modcenter "cac\vista_heroe.py"
    if (Test-Path -LiteralPath $vh) { Copy-Item -LiteralPath $vh (Join-Path $cacd "vista_heroe.py") }
    # studies 9b: prenda de Xenoverse (lee el data.cpk del propio jugador; no se publica nada del juego)
    $rx = Join-Path $modcenter "cac\ropa_xeno.py"
    if (Test-Path -LiteralPath $rx) { Copy-Item -LiteralPath $rx (Join-Path $cacd "ropa_xeno.py") }
    foreach ($f in @("model_render","roster_build","capsulas","colores","gritos","hornada_support","voces")) {
        $src = Join-Path $modcenter "$f.py"
        if (Test-Path -LiteralPath $src) { Copy-Item -LiteralPath $src (Join-Path $mcd "$f.py") }
    }
    $libd = Join-Path $mcd "lib"
    New-Item -ItemType Directory -Path $libd -Force | Out-Null
    foreach ($f in @("awg_vertex_buffer","b3_gateway","skin_slots","injertar_pelo","injertar_ropa","xeno_model","crilayla","cri")) {
        $src = Join-Path $root "awo_tools\$f.py"
        if (Test-Path -LiteralPath $src) { Copy-Item -LiteralPath $src (Join-Path $libd "$f.py") }
    }
}

# 1.5.1.2: escenarios de PS2 (B1/B2/IW desde las ISO del jugador, al pulsar JUGAR) y vista previa del Modo Heroe
# SIN Kit ni Python instalados. make_modpacks.py --game-stage anade al stage los scripts con TODOS sus imports
# (mod center hd/ y awo_tools/, las mismas rutas que el Kit), sus datos y python\ portatil (numpy + Pillow; el
# launcher lo prefiere: mod_pipeline.cpp PythonCandidates). Nada del juego viaja; el Kit encima no pisa nada distinto.
$py = (Get-Command python -ErrorAction Stop).Source
& $py (Join-Path $root "tools\make_modpacks.py") --game-stage $OutDir
if ($LASTEXITCODE -ne 0) { throw "make_modpacks.py --game-stage fallo" }

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
