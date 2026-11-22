# Builds XMOW.nro (gears2-nx app layer over ReXGlue SDK NX + deko3d) and assembles out/sd/switch/XMOW/.
#   powershell -ExecutionPolicy Bypass -File tools/build.ps1 -DevkitPro <devkitpro> [-Python python] [-Cmake cmake] [-Ninja ninja]
#        [-Jobs 8] [-Versao 0.1.9-diag] [-Icone <256x256.jpg>] [-Reconfigurar] [-SomenteConfigurar]
# Inputs: sdk/ (tools/prepare_sdk.py), out/codegen (tools/codegen.ps1), out/shaders/out (tools/shaders.ps1),
#         assets/game_root (tools/extract_iso.py). The ELF is kept next to the NRO hash for addr2line on crash logs.
param(
    [Parameter(Mandatory = $true)][string]$DevkitPro,
    [string]$Python = 'python',
    [string]$Cmake = 'cmake',
    [string]$Ninja = 'ninja',
    [int]$Jobs = 8,
    [string]$Versao = '0.1.9-diag',
    [string]$Icone = '',
    [switch]$Reconfigurar,
    [switch]$SomenteConfigurar
)
$ErrorActionPreference = 'Stop'
$Root = Split-Path $PSScriptRoot -Parent
$source = Join-Path $Root 'app'
$sdk = Join-Path $Root 'sdk'
$codegen = Join-Path $Root 'out\codegen'
$build = Join-Path $Root 'out\build'
$logs = Join-Path $Root 'out\logs'
$elfDir = Join-Path $Root 'out\elf'
$sd = Join-Path $Root 'out\sd\switch\XMOW'
$shaders = Join-Path $Root 'out\shaders\out'
New-Item -ItemType Directory -Path $logs, $elfDir -Force | Out-Null
$env:PYTHONDONTWRITEBYTECODE = '1'
$env:DEVKITPRO = $DevkitPro.Replace('\', '/')
$env:PATH = "$(Join-Path $DevkitPro 'tools\bin');$env:PATH"
$ninjaPath = (Get-Command $Ninja).Source
$pythonPath = (Get-Command $Python).Source

if (-not (Test-Path (Join-Path $sdk '.xmow-sdk-prepared'))) { throw 'sdk/ not prepared: run tools/prepare_sdk.py' }
if (-not (Test-Path (Join-Path $codegen 'generated\default\sources.cmake'))) { throw 'out/codegen/generated/default/sources.cmake missing: run tools/codegen.ps1' }

$configLog = Join-Path $logs 'configure.log'
$buildLog = Join-Path $logs 'build.log'
if ($Reconfigurar -or -not (Test-Path (Join-Path $build 'build.ninja'))) {
    $arguments = @(
        '-S', $source, '-B', $build, '-G', 'Ninja',
        '-DCMAKE_BUILD_TYPE=Release',
        "-DCMAKE_TOOLCHAIN_FILE=$($source.Replace('\','/'))/toolchain.cmake",
        "-DCMAKE_MAKE_PROGRAM=$($ninjaPath.Replace('\','/'))",
        "-DDEVKITPRO=$env:DEVKITPRO",
        "-DPython3_EXECUTABLE=$($pythonPath.Replace('\','/'))",
        "-DREXSDK_DIR=$($sdk.Replace('\','/'))",
        "-DGOW2_CODEGEN_DIR=$($codegen.Replace('\','/'))",
        '-DREXGLUE_SWITCH_DEKO3D=ON',
        '-DREXGLUE_USE_VULKAN=OFF',
        '-DFETCHCONTENT_FULLY_DISCONNECTED=ON',
        "-DXMOW_NRO_VERSION=$Versao"
    )
    if ($Icone) { $arguments += "-DXMOW_ICON=$((Resolve-Path $Icone).Path.Replace('\','/'))" }
    $ErrorActionPreference = 'Continue'
    & $Cmake @arguments *> $configLog
    $configureCode = $LASTEXITCODE
    $ErrorActionPreference = 'Stop'
    if ($configureCode -ne 0) { Get-Content -LiteralPath $configLog -Tail 40; exit $configureCode }
    Write-Output "configure ok ($configLog)"
}
if ($SomenteConfigurar) { exit 0 }

$sw = [Diagnostics.Stopwatch]::StartNew()
$ErrorActionPreference = 'Continue'
& $Cmake --build $build --target xmow --parallel $Jobs *> $buildLog
$code = $LASTEXITCODE
$ErrorActionPreference = 'Stop'
Get-Content -LiteralPath $buildLog -Tail 20
"build time: $($sw.Elapsed)"
if ($code -ne 0) { exit $code }

$nro = Join-Path $build 'xmow.nro'
$hash = (Get-FileHash -LiteralPath $nro -Algorithm SHA256).Hash
Copy-Item -LiteralPath (Join-Path $build 'xmow') -Destination (Join-Path $elfDir ("{0}_{1}.elf" -f $Versao, $hash.Substring(0, 8))) -Force

# SD folder: NRO, xmow.toml, shaders and the extracted disc
New-Item -ItemType Directory -Path $sd -Force | Out-Null
Copy-Item -LiteralPath $nro -Destination (Join-Path $sd 'XMOW.nro') -Force
Copy-Item -LiteralPath (Join-Path $source 'xmow.toml') -Destination (Join-Path $sd 'xmow.toml') -Force
foreach ($n in 'xmow_shaders.dslib', 'shadercache_dksh.bin') {
    $p = Join-Path $shaders $n
    if (Test-Path $p) { Copy-Item -LiteralPath $p -Destination (Join-Path $sd $n) -Force } else { Write-Warning "$n missing: run tools/shaders.ps1" }
}
Write-Output "XMOW.nro SHA-256 $hash"
Write-Output "SD folder: $sd (copy assets/game_root to $sd\game_root)"
