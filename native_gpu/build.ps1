param(
    [Parameter(Mandatory=$true)][string]$Zig,
    [Parameter(Mandatory=$true)][string]$OutputDirectory,
    [string]$Python = 'python'
)
$ErrorActionPreference = 'Stop'
$compilerPath = (Resolve-Path -LiteralPath $Zig).Path
$nativeOutput = [IO.Path]::GetFullPath($OutputDirectory)
New-Item -ItemType Directory -Path $nativeOutput -Force | Out-Null
$nativeSource = Join-Path $PSScriptRoot 'native_renderer.cpp'
$flags = @('c++', '-std=c++17', '-O2', '-fms-extensions', '-target', 'x86_64-windows-gnu')
$libraries = @('-ld3d12','-ldxgi','-ld3dcompiler_47','-ldxguid','-luser32','-lgdi32','-lole32')
& $compilerPath @flags $nativeSource -o (Join-Path $nativeOutput 'dbz3-native-gpu.exe') @libraries
if ($LASTEXITCODE -ne 0) { throw 'Native viewer compilation failed' }
& $compilerPath @flags -shared -DDBZ_NATIVE_DLL $nativeSource -o (Join-Path $nativeOutput 'dbz3-native-renderer.dll') @libraries
if ($LASTEXITCODE -ne 0) { throw 'Native renderer DLL compilation failed' }
Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'materials.hlsl') -Destination $nativeOutput -Force
& $Python (Join-Path $PSScriptRoot 'build_registry.py') --output (Join-Path $nativeOutput 'materials.dbzregistry')
if ($LASTEXITCODE -ne 0) { throw 'Material registry generation failed' }
