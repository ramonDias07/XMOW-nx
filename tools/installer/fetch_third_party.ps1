# Fetches the two large third-party trees the uam tools need, at the pinned commits (same as sdk-g2 tools/switch_setup.py):
#   SPIRV-Cross  https://github.com/KhronosGroup/SPIRV-Cross  f11ba9f0b21ba8fc15153d50a2a1ae31ab1cf8f7 (whole tree)
#   uam          https://github.com/centollOS/SwitchWakerHD    2d147aaaa27b1ad0f9b390eff515609af9fe24a3 runtime/third_party/uam
# Usage: powershell -ExecutionPolicy Bypass -File fetch_third_party.ps1 [-Dest <folder>]   (default third_party\_upstream)
# Needs git. Nothing else is downloaded: xxHash, fmt and libmspack are in third_party\ as used.
param([string]$Dest = "$PSScriptRoot\third_party\_upstream")
$ErrorActionPreference = 'Stop'
function Get-Pinned($Url, $Commit, $Folder, $Out) {
  if (Test-Path "$Out\.ok") { "$Out present"; return }
  if (Test-Path $Out) { Remove-Item -Recurse -Force $Out }
  New-Item -ItemType Directory -Force $Out | Out-Null
  git -C $Out init -q
  git -C $Out remote add origin $Url
  if ($Folder) {
    git -C $Out sparse-checkout set $Folder
    git -C $Out fetch -q --depth 1 --filter=blob:none origin $Commit
  } else {
    git -C $Out fetch -q --depth 1 origin $Commit
  }
  git -C $Out checkout -q FETCH_HEAD
  if ($LASTEXITCODE) { throw "git checkout of $Url failed" }
  $got = git -C $Out rev-parse HEAD
  if ($got -ne $Commit) { throw "$Url gave $got, expected $Commit" }
  Set-Content "$Out\.ok" $Commit
  "fetched $Url $Commit"
}
Get-Pinned 'https://github.com/KhronosGroup/SPIRV-Cross.git' 'f11ba9f0b21ba8fc15153d50a2a1ae31ab1cf8f7' '' "$Dest\SPIRV-Cross"
Get-Pinned 'https://github.com/centollOS/SwitchWakerHD.git' '2d147aaaa27b1ad0f9b390eff515609af9fe24a3' 'runtime/third_party/uam' "$Dest\SwitchWakerHD"
