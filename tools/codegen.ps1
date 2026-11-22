# Generates the recompiled Wolverine code (gears2-nx style) from your own default.xex into out/codegen/generated/default.
#   powershell -ExecutionPolicy Bypass -File tools/codegen.ps1 -Rexglue <rexglue(.exe)> [-Python <python>] [-Xex <default.xex>]
#                                                             [-SemBarreiras] [-SemLocalRegs]
# rexglue: the host codegen tool of ReXGlue SDK 0.10 (see docs/build.md). On Windows its folder (and the llvm-mingw bin
# folder, for libunwind) must be on PATH or given with -ExtraPath.
# The 0.1.x-diag builds were made WITHOUT patch_local_regs (-SemLocalRegs): see docs/estado.md, test 6.
param(
    [Parameter(Mandatory = $true)][string]$Rexglue,
    [string]$Python = 'python',
    [string]$Xex = '',
    [string[]]$ExtraPath = @(),
    [switch]$SemBarreiras,
    [switch]$SemLocalRegs
)
$ErrorActionPreference = 'Stop'
$Root = Split-Path $PSScriptRoot -Parent
$Tools = Join-Path $PSScriptRoot 'codegen'
$Sdk = Join-Path $Root 'sdk'
$D = Join-Path $Root 'out\codegen'
$Gen = Join-Path $D 'generated\default'
if (-not $Xex) { $Xex = Join-Path $Root 'assets\game_root\default.xex' }
if (-not (Test-Path $Xex)) { throw "default.xex not found: $Xex (run tools/extract_iso.py first)" }
if (-not (Test-Path (Join-Path $Sdk '.xmow-sdk-prepared'))) { throw 'sdk/ not prepared: run tools/prepare_sdk.py' }
$env:PATH = ((@(Split-Path (Resolve-Path $Rexglue)) + $ExtraPath) -join ';') + ";$env:PATH"
New-Item -ItemType Directory -Force (Join-Path $D 'tmp') | Out-Null
$env:TEMP = Join-Path $D 'tmp'; $env:TMP = $env:TEMP
Set-Location $D

function Passo($nome, $cmdline) {
    # via cmd, so stdout+stderr go to the log without PowerShell 5.1 NativeCommandError
    Write-Host "== $nome"
    cmd /c "$cmdline > `"$D\$nome.log`" 2>&1"
    if ($LASTEXITCODE -ne 0) { Get-Content "$D\$nome.log" -Tail 20; throw "$nome failed ($LASTEXITCODE)" }
    Get-Content "$D\$nome.log" -Tail 3
}
$Run = "`"$Python`" -I"

if ((Get-FileHash $Xex -Algorithm SHA256).Hash -ne '2F987F7BA3415BF9D1BC63BAC3E671FD534EB33BB53878E816500C01BCD15378') {
    Write-Warning 'default.xex is not the tested one (Uncaged Edition USA/Europe, no title update)'
}

# 1. manifest = app/codegen manifest + config + names.toml
$App = Join-Path $Root 'app\codegen'
Passo 'merge_names' "$Run `"$Tools\merge_names.py`" `"$App\xmow_manifest.toml`" `"$App\xmow_config.toml`" `"$App\names.toml`" `"$D\xmow_manifest.toml`" `"$D\xmow_config.toml`" `"$Sdk`" `"$Xex`""

# 2. codegen (expected: ~61,236 functions)
if (Test-Path "$D\generated") { Remove-Item -Recurse -Force "$D\generated" }
Passo 'codegen' "`"$Rexglue`" codegen xmow_manifest.toml"

# 3. passes over the generated code, in the order of gears2-nx codegen.sh (the two Gears-only passes are skipped)
Passo 'patch_xmow_jogo' "$Run `"$Tools\patch_xmow_jogo.py`" `"generated/default/*.cpp`""
Passo 'patch_switch_traps' "$Run `"$Tools\patch_switch_traps.py`" `"generated/default/*.cpp`""
if (-not $SemBarreiras) {
    Passo 'patch_memory_barriers' "$Run `"$Tools\patch_memory_barriers.py`" `"generated/default/*.cpp`""
}
Passo 'patch_spin_hints' "$Run `"$Tools\glob_run.py`" `"$Tools\run_spin_hints.py`" `"$Tools`" `"$D\generated`" `"generated/default/*.cpp`""
Passo 'patch_leaf_prologue' "$Run `"$Tools\glob_run.py`" `"$Tools\patch_leaf_prologue.py`" `"generated/default/*.cpp`""
Passo 'patch_stack_access'  "$Run `"$Tools\glob_run.py`" `"$Tools\patch_stack_access.py`" `"generated/default/*.cpp`""
# the app hooks (REX_HOOK sub_XXXXXXXX) keep their call
$Src = "`"$Root\app\src`" `"$Sdk\src`" `"$Sdk\include`""
Passo 'patch_inline_calls' "$Run `"$Tools\glob_run.py`" `"$Tools\patch_inline_calls.py`" $Src -- `"generated/default/xmow_recomp.*.cpp`""
if (-not $SemLocalRegs) {
    Passo 'patch_local_regs' "$Run `"$Tools\glob_run.py`" `"$Tools\patch_local_regs.py`" `"generated/default/xmow_inline_helpers.h`" $Src -- `"generated/default/xmow_recomp.*.cpp`""
}
$n = (Get-ChildItem "$Gen\xmow_recomp.*.cpp").Count
Write-Host "done: $n recomp files in $Gen (the CMake configure mirrors them with the Switch PCH)"
