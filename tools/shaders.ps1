# Shader pipeline of gears2-nx (.dslib + DKSH shadercache) for XMOW, on Windows. Re-runnable. Output in out/shaders/out.
#   powershell -ExecutionPolicy Bypass -File tools/shaders.ps1 -LlvmBin <llvm-mingw\bin> -Dxc <dxc.exe> -SpirvVal <spirv-val.exe>
#        [-Python python] [-Extras <shaders_dump folders>] [-Etapas ferramentas,extrair,entrada,hlsl,spirv,dslib,dksh,validar] [-Jobs 10]
# Needs CMake and Ninja on PATH (or -ExtraPath), and the prepared sdk/ (tools/prepare_sdk.py) for SPIRV-Cross, uam, xxHash,
# fmt, libmspack and the deko3d GLSL/DKSH cache code.
param(
  [Parameter(Mandatory = $true)][string]$LlvmBin,
  [Parameter(Mandatory = $true)][string]$Dxc,
  [Parameter(Mandatory = $true)][string]$SpirvVal,
  [string]$Python = 'python',
  [string[]]$Extras = @(),
  [string[]]$ExtraPath = @(),
  [string[]]$Etapas = @('ferramentas', 'extrair', 'entrada', 'hlsl', 'spirv', 'dslib', 'dksh', 'validar'),
  [int]$Jobs = 10,
  [int]$LimiteVariante = 120
)
$ErrorActionPreference = 'Stop'
$Root = Split-Path $PSScriptRoot -Parent
$T = Join-Path $PSScriptRoot 'shaders'
$f = Join-Path $T 'src'
$S = Join-Path $Root 'out\shaders'
$Sdk = Join-Path $Root 'sdk'
$Terceros = Join-Path $Sdk 'thirdparty'
$Cookedxenon = Join-Path $Root 'assets\game_root\WGame\CookedXenon'
if (-not (Test-Path (Join-Path $Sdk '.xmow-sdk-prepared'))) { throw 'sdk/ not prepared: run tools/prepare_sdk.py' }
$env:PATH = ((@($LlvmBin) + $ExtraPath) -join ';') + ";$env:PATH"
$env:JOBS = "$Jobs"
$env:XMOW_SHADERS_OUT = $S
$env:DXC = $Dxc
$env:SPIRV_VAL = $SpirvVal
New-Item -ItemType Directory -Force "$S\bin", "$S\out", "$S\logs", "$S\bld" | Out-Null

function Paso($t) { Write-Host "== $t" -ForegroundColor Cyan }
function Run($exe, [string[]]$a) { & $exe @a; if ($LASTEXITCODE -ne 0) { throw "$exe exited with $LASTEXITCODE" } }

if ($Etapas -contains 'ferramentas') {
  Paso 'tools (clang llvm-mingw, static)'
  $cxx = @('-std=c++2b', '-O1', '-static', "-I$f", "-I$f\XenosRecomp", "-I$Terceros\fmt\include", "-I$Terceros\xxHash",
           '-include', "$f\XenosRecomp\pch_min.h", '-DFMT_HEADER_ONLY', '-DXXH_INLINE_ALL', '-Wno-switch', '-Wno-unused-variable',
           '-Wno-null-arithmetic', '-fms-extensions', '-fexperimental-library')
  Run clang++ ($cxx + @("$f\ds_hlsl.cpp", "$f\XenosRecomp\shader_recompiler.cpp", '-o', "$S\bin\ds_hlsl.exe"))
  Run clang++ @('-std=c++20', '-O1', '-static', "-I$f", "-I$Terceros\xxHash", "$f\ds_empaquetar.cpp", "$f\ds_shader_library.cpp", '-o', "$S\bin\ds_empaquetar.exe")
  $M = "$Terceros\libmspack\libmspack\mspack"
  New-Item -ItemType Directory -Force "$S\bld\extrator" | Out-Null
  Run clang @('-O2', '-w', "-I$M", '-c', "$M\lzxd.c", '-o', "$S\bld\extrator\lzxd.o")
  Run clang++ @('-std=c++20', '-O2', '-static', "-I$M", "-I$Terceros\xxHash", "$f\xmow_extract_shaders.cpp", "$S\bld\extrator\lzxd.o", '-o', "$S\bin\xmow_extract_shaders.exe")
  # ds_dksh + ds_validar (uam + SPIRV-Cross + glsl.cpp/cache_dksh.cpp of the patched SDK)
  Run cmake @('-S', "$f\dksh", '-B', "$S\bld\dksh", '-G', 'Ninja', '-DCMAKE_BUILD_TYPE=Release', "-DDS_TERCEROS=$Terceros", "-DDS_SDK_MIN=$Sdk")
  Run cmake @('--build', "$S\bld\dksh", '-j', "$Jobs")
  Copy-Item "$S\bld\dksh\ds_dksh.exe", "$S\bld\dksh\ds_validar.exe" "$S\bin" -Force
}
if ($Etapas -contains 'extrair') {
  Paso 'extract the shader containers from the .xxx packages of your disc'
  if (Test-Path "$S\out\extraidos") { Remove-Item -Recurse -Force "$S\out\extraidos" }
  & "$S\bin\xmow_extract_shaders.exe" "$S\out\extraidos" $Cookedxenon *> "$S\logs\extrator.log"
  if ($LASTEXITCODE -ne 0) { Get-Content "$S\logs\extrator.log" -Tail 20; throw 'xmow_extract_shaders failed' }
  Get-Content "$S\logs\extrator.log" -Tail 1
}
if ($Etapas -contains 'entrada') {
  Paso 'input: extracted containers + console dumps (no repeated content)'
  if (Test-Path "$S\out\entrada") { Remove-Item -Recurse -Force "$S\out\entrada" }
  Run $Python (@('-I', "$T\preparar_entrada.py", "$S\out\entrada", "$S\out\extraidos") + $Extras)
}
if ($Etapas -contains 'hlsl') { Paso 'ds_hlsl (--saltos-lineales)'; Run $Python @('-I', "$T\etapas.py", 'hlsl') }
if ($Etapas -contains 'spirv') { Paso 'DXC + spirv-val'; Run $Python @('-I', "$T\etapas.py", 'spirv') }
if ($Etapas -contains 'dslib') { Paso 'ds_empaquetar -> xmow_shaders.dslib'; Run $Python @('-I', "$T\etapas.py", 'empaquetar') }
if ($Etapas -contains 'dksh') {
  Paso 'ds_dksh -> shadercache_dksh.bin'
  Remove-Item -Force "$S\out\shadercache_dksh.bin" -ErrorAction SilentlyContinue
  & "$S\bin\ds_dksh.exe" "$S\out\xmow_shaders.dslib" "$S\out\shadercache_dksh.bin" --contenedores "$S\out\contenedores" `
    --informe "$S\out\dksh.tsv" --hilos $Jobs --limite $LimiteVariante --max-por-proceso 400 --diario "$S\out\dksh_diarios" *> "$S\logs\dksh.log"
  Write-Host "ds_dksh exited with $LASTEXITCODE (2 = some variant failed: see out\dksh.tsv)"
  Get-Content "$S\logs\dksh.log" -Tail 12
}
if ($Etapas -contains 'validar') {
  Paso 'ds_validar (reads .dslib and cache like the runtime)'
  & "$S\bin\ds_validar.exe" "$S\out\xmow_shaders.dslib" "$S\out\shadercache_dksh.bin" "$S\out\shaders_sem_dksh.tsv" *> "$S\out\validacao.txt"
  $v = $LASTEXITCODE
  Get-Content "$S\out\validacao.txt"
  Get-FileHash "$S\out\xmow_shaders.dslib", "$S\out\shadercache_dksh.bin" -Algorithm SHA256 | Format-Table Hash, Path -AutoSize
  Write-Host "validation: code $v"
}
