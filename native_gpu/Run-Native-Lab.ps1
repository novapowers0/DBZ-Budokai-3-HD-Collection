param(
    [Parameter(Mandatory=$true)][string]$LabDirectory,
    [Parameter(Mandatory=$true)][string]$NativeDirectory,
    [Parameter(Mandatory=$true)][string]$GameDataRoot,
    [switch]$AuditInputState,
    [switch]$AuditMemory
)
$ErrorActionPreference = 'Stop'
$labPath = (Resolve-Path -LiteralPath $LabDirectory).Path
$nativePath = (Resolve-Path -LiteralPath $NativeDirectory).Path
$dataPath = (Resolve-Path -LiteralPath $GameDataRoot).Path
$required = @(
    (Join-Path $labPath 'dbz3-gpu-lab.exe'),
    (Join-Path $labPath 'rexruntime.dll'),
    (Join-Path $nativePath 'dbz3-native-renderer.dll'),
    (Join-Path $nativePath 'materials.hlsl'),
    (Join-Path $nativePath 'materials.dbzregistry'),
    (Join-Path $dataPath 'yae3_xenon.xex'),
    (Join-Path $dataPath 'eu')
)
foreach ($path in $required) {
    if (-not (Test-Path -LiteralPath $path)) { throw "Missing required local input: $path" }
}
$saved = @{}
Get-ChildItem Env:DBZ3_NATIVE_* | ForEach-Object { $saved[$_.Name] = $_.Value }
try {
    Get-ChildItem Env:DBZ3_NATIVE_* | Remove-Item
    $env:DBZ3_NATIVE_GRAPHICS = '1'
    $env:DBZ3_NATIVE_STREAM = '1'
    $env:DBZ3_NATIVE_RENDERER = Join-Path $nativePath 'dbz3-native-renderer.dll'
    $env:DBZ3_NATIVE_SHADERS = Join-Path $nativePath 'materials.hlsl'
    $env:DBZ3_NATIVE_MATERIALS = Join-Path $nativePath 'materials.dbzregistry'
    if ($AuditInputState) { $env:DBZ3_NATIVE_INPUT_AUDIT = '1' }
    if ($AuditMemory) { $env:DBZ3_NATIVE_MEMORY_AUDIT = '1' }
    Push-Location -LiteralPath $labPath
    try {
        & (Join-Path $labPath 'dbz3-gpu-lab.exe') "--game_data_root=$dataPath"
        if ($LASTEXITCODE -ne 0) { throw "Native lab exited with code $LASTEXITCODE" }
    } finally { Pop-Location }
} finally {
    Get-ChildItem Env:DBZ3_NATIVE_* | Remove-Item
    foreach ($name in $saved.Keys) { [Environment]::SetEnvironmentVariable($name, $saved[$name], 'Process') }
}
