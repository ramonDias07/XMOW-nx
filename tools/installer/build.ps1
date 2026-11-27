# Builds xmow_setup.exe and every tool it runs, from the sources in this folder, and lays out the distribution folder.
#   powershell -ExecutionPolicy Bypass -File build.ps1 [-Out <build folder>] [-Upstream <fetch_third_party.ps1 dest>]
#              [-Dxc <folder with dxc.exe + dxcompiler.dll>] [-Jobs N]
# Toolchain (on PATH, or -Llvm/-Cmake/-Ninja): llvm-mingw 20261020 ucrt x86_64 (clang 22), CMake >= 3.20, Ninja.
# DXC: DirectXShaderCompiler v1.8.2407 (dxc_2024_07_31, x64 bin): dxc.exe + dxcompiler.dll (dxil.dll is not needed).
# The compiler flags are the ones the shipped binaries were built with (shaders-g2 gerar.ps1, fuente\dksh\CMakeLists.txt,
# 0.1.33-shaders construir-herramientas.ps1). Two uam builds: ds_dksh_0123 = upstream uam; ds_dksh_0127 and
# ds_vs_plegado = uam + patches\uam-postfactor-0127.patch.
param(
  [string]$Out = "$PSScriptRoot\build",
  [string]$Upstream = "$PSScriptRoot\third_party\_upstream",
  [string]$Dxc = '',
  [string]$Llvm = '', [string]$Cmake = '', [string]$Ninja = '',
  [int]$Jobs = 0
)
$ErrorActionPreference = 'Stop'
foreach ($d in $Llvm, $Cmake, $Ninja) { if ($d) { $env:PATH = "$d;$env:PATH" } }
if ($Jobs -lt 1) { $Jobs = if ($env:FILA_JOBS) { [int]$env:FILA_JOBS } else { [int]$env:NUMBER_OF_PROCESSORS } }
$R = $PSScriptRoot
$F = "$R\tool-src\shaders-g2"
$V = "$R\tool-src\vs-plegado"
$TP = "$R\third_party"
$M = "$TP\libmspack\mspack"
$Bin = "$Out\bin"
New-Item -ItemType Directory -Force $Bin, "$Out\obj" | Out-Null
function Run($exe, [string[]]$a) { & $exe @a; if ($LASTEXITCODE -ne 0) { throw "$exe failed with $LASTEXITCODE" } }

foreach ($n in 'SPIRV-Cross\CMakeLists.txt', 'SwitchWakerHD\runtime\third_party\uam\CMakeLists.txt') {
  if (-not (Test-Path "$Upstream\$n")) { throw "missing $Upstream\$n (run fetch_third_party.ps1 first)" }
}

"== ds_hlsl"
Run clang++ @('-std=c++2b', '-O1', '-static', "-I$F", "-I$F\XenosRecomp", "-I$TP\fmt\include", "-I$TP\xxHash",
  '-include', "$F\XenosRecomp\pch_min.h", '-DFMT_HEADER_ONLY', '-DXXH_INLINE_ALL', '-Wno-switch', '-Wno-unused-variable',
  '-Wno-null-arithmetic', '-fms-extensions', '-fexperimental-library', "$F\ds_hlsl.cpp", "$F\XenosRecomp\shader_recompiler.cpp",
  '-o', "$Bin\ds_hlsl.exe")
"== ds_empaquetar"
Run clang++ @('-std=c++20', '-O1', '-static', "-I$F", "-I$TP\xxHash", "$F\ds_empaquetar.cpp", "$F\ds_shader_library.cpp", '-o', "$Bin\ds_empaquetar.exe")
"== xmow_extract_shaders (+ libmspack lzxd)"
Run clang @('-O2', '-w', "-I$M", '-c', "$M\lzxd.c", '-o', "$Out\obj\lzxd.o")
Run clang++ @('-std=c++20', '-O2', '-static', "-I$M", "-I$TP\xxHash", "$F\xmow_extract_shaders.cpp", "$Out\obj\lzxd.o", '-o', "$Bin\xmow_extract_shaders.exe")

# ds_dksh: fuente\dksh\CMakeLists.txt wants one folder with uam, SPIRV-Cross and xxHash (DS_TERCEROS)
foreach ($k in '0123', '0127') {
  "== ds_dksh_$k"
  $t = "$Out\terceros-$k"
  if (Test-Path $t) {
    # unlink the SPIRV-Cross junction first: a recursive delete must not reach the fetched tree
    if (Test-Path "$t\SPIRV-Cross") { & cmd /c rmdir "$t\SPIRV-Cross" 2>$null }
    if (Test-Path "$t\SPIRV-Cross") { throw "$t\SPIRV-Cross is not a junction; delete $t by hand" }
    Remove-Item -Recurse -Force $t
  }
  New-Item -ItemType Directory -Force $t | Out-Null
  Copy-Item -Recurse "$Upstream\SwitchWakerHD\runtime\third_party\uam" "$t\uam"
  # SPIRV-Cross is used as fetched (a junction: its test tree has paths longer than MAX_PATH)
  & cmd /c mklink /J "$t\SPIRV-Cross" "$Upstream\SPIRV-Cross" | Out-Null
  if (-not (Test-Path "$t\SPIRV-Cross\CMakeLists.txt")) { throw "junction to $Upstream\SPIRV-Cross failed" }
  Copy-Item -Recurse "$TP\xxHash" "$t\xxHash"
  if ($k -eq '0127') {
    Push-Location "$t\uam"
    Run git @('apply', '--whitespace=nowarn', "$R\patches\uam-postfactor-0127.patch")
    Pop-Location
  }
  Run cmake @('-S', "$F\dksh", '-B', "$Out\dksh-$k", '-G', 'Ninja', '-DCMAKE_BUILD_TYPE=Release', '-DCMAKE_C_COMPILER=clang',
    '-DCMAKE_CXX_COMPILER=clang++', "-DDS_TERCEROS=$t", "-DDS_SDK_MIN=$F\sdk-min")
  Run cmake @('--build', "$Out\dksh-$k", '--target', 'ds_dksh', '-j', "$Jobs")
  Copy-Item "$Out\dksh-$k\ds_dksh.exe" "$Bin\ds_dksh_$k.exe" -Force
}

"== ds_vs_plegado (uam + SPIRV-Cross of the 0127 build)"
$t = "$Out\terceros-0127"; $L = "$Out\dksh-0127"
Run clang++ @('-std=gnu++20', '-O2', '-DNDEBUG', '-static', "-I$V", "-I$F", "-I$V\sdk-g2\include", "-I$t\xxHash", "-I$t\uam", "-I$t\SPIRV-Cross",
  "$V\ds_vs_plegado.cpp", "$V\sdk-g2\src\ui\deko3d\glsl.cpp", "$F\ds_shader_library.cpp",
  "$L\uam\libuamlib.a", "$L\spirv-cross\libspirv-cross-glsl.a", "$L\spirv-cross\libspirv-cross-core.a", '-lpthread',
  '-o', "$Bin\ds_vs_plegado.exe")

"== xmow_setup"
Run clang++ @('-std=c++20', '-O2', '-static', '-municode', '-Wall', '-Wextra', '-Wno-unused-parameter', "-I$TP\xxHash",
  "$R\src\main.cpp", "$R\src\disco.cpp", '-o', "$Bin\xmow_setup.exe", '-lpsapi')

"== dist"
$D = "$Out\dist"
if (Test-Path $D) { Remove-Item -Recurse -Force $D }
New-Item -ItemType Directory -Force "$D\tools", "$D\data", "$D\licenses" | Out-Null
Copy-Item "$Bin\xmow_setup.exe", "$R\README.txt" $D
Copy-Item "$Bin\ds_hlsl.exe", "$Bin\ds_empaquetar.exe", "$Bin\xmow_extract_shaders.exe", "$Bin\ds_dksh_0123.exe", "$Bin\ds_dksh_0127.exe",
  "$Bin\ds_vs_plegado.exe" "$D\tools"
Copy-Item "$R\data\*" "$D\data"
Copy-Item "$F\XenosRecomp\shader_common.h" "$D\data"
Copy-Item "$R\licenses\*" "$D\licenses"
if ($Dxc) { Copy-Item "$Dxc\dxc.exe", "$Dxc\dxcompiler.dll" "$D\tools" } else { "(no -Dxc: copy dxc.exe and dxcompiler.dll into $D\tools)" }
Get-ChildItem $Bin -Filter *.exe | ForEach-Object { "{0,-26} {1,10} {2}" -f $_.Name, $_.Length, (Get-FileHash $_.FullName -Algorithm SHA256).Hash }
