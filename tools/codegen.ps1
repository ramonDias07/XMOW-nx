# Generates the recompiled Wolverine code (gears2-nx style, build 0.1.34f recipe) from your own default.xex into
# out/codegen/generated/default.
#   powershell -ExecutionPolicy Bypass -File tools/codegen.ps1 -Rexglue <rexglue(.exe)> [-Python <python>] [-Xex <default.xex>]
#                                                             [-ExtraPath <dirs>] [-SemBarreiras] [-SemLocalRegs] [-SemVolatil]
#                                                             [-GeradorAntigo]
# rexglue: the code generator. Default recipe (batch C): the LoS1-nx fork generator built by tools/prepare_generator.py,
#   run with the options of app/codegen/xmow_flavor_lote1.toml. With -GeradorAntigo use the plain ReXGlue SDK 0.10 host
#   tool instead (the 0.1.18 recipe, no flavor file).
# On Windows the generator's folder (and the llvm-mingw bin folder, for libunwind) must be on PATH or given with -ExtraPath.
# Batches: A = pch_no_volatile + loop_barriers (-SemVolatil turns both off), B = patch_local_regs with the exception list
#   app/codegen/local_regs_keep.txt (-SemLocalRegs turns it off), C = fork generator + flavor options. The 0.1.0-0.1.16
#   builds were made without B (docs/estado.md, test 6).
param(
    [Parameter(Mandatory = $true)][string]$Rexglue,
    [string]$Python = 'python',
    [string]$Xex = '',
    [string[]]$ExtraPath = @(),
    [switch]$SemBarreiras,
    [switch]$SemLocalRegs,
    [switch]$SemVolatil,
    [switch]$GeradorAntigo
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

# 1. manifest = app/codegen manifest + config + names.toml (+ the fork generator's flavor options, batch C)
$App = Join-Path $Root 'app\codegen'
Passo 'merge_names' "$Run `"$Tools\merge_names.py`" `"$App\xmow_manifest.toml`" `"$App\xmow_config.toml`" `"$App\names.toml`" `"$D\xmow_manifest.toml`" `"$D\xmow_config.toml`" `"$Sdk`" `"$Xex`""
if (-not $GeradorAntigo) {
    Copy-Item -LiteralPath "$App\xmow_flavor_lote1.toml" -Destination "$D\xmow_flavor_lote1.toml" -Force
    $man = [IO.File]::ReadAllText("$D\xmow_manifest.toml")
    $old = "includes = [`n  `"xmow_config.toml`",`n]"
    if (-not $man.Contains($old)) { throw 'xmow_manifest.toml: includes block not found' }
    $man = $man.Replace($old, "includes = [`n  `"xmow_config.toml`",`n  `"xmow_flavor_lote1.toml`",`n]")
    [IO.File]::WriteAllText("$D\xmow_manifest.toml", $man, (New-Object Text.UTF8Encoding $false))
}

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
    $env:PLR_KEEP_REPORT = "$D\patch_local_regs.keep.txt"
    Passo 'patch_local_regs' "$Run `"$Tools\glob_run.py`" `"$Tools\patch_local_regs.py`" --keep `"$App\local_regs_keep.txt`" `"generated/default/xmow_inline_helpers.h`" $Src -- `"generated/default/xmow_recomp.*.cpp`""
    $env:PLR_KEEP_REPORT = $null
}
# Batch A: guest loads/stores without volatile in xmow_pch.h + REX_LOOP_BARRIER() in loop headers. The two go together
# (without the barriers a memory wait loop becomes "b ." under GCC -O3). Last, after every pass that rewrites bodies.
if (-not $SemVolatil) {
    Passo 'pch_no_volatile' "$Run `"$Tools\pch_no_volatile.py`" `"$Gen\xmow_pch.h`""
    Passo 'loop_barriers'   "$Run `"$Tools\loop_barriers.py`" `"$Gen`" --report `"$D\loop_barriers.tsv`""
}
# The CMake configure (app/tools/prepare_tree.py, app/tools/make_nx_pch.py + xmow_diet_ops.h) mirrors the generated
# code with the Switch PCH; the app copies of those scripts are the ones the build uses.
$n = (Get-ChildItem "$Gen\xmow_recomp.*.cpp").Count
Write-Host "done: $n recomp files in $Gen (the CMake configure mirrors them with the Switch PCH)"
