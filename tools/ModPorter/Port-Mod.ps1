<#
.SYNOPSIS
  Bedrock Forge ModPorter: converts a Forge/MCreator/GeckoLib mod jar YOU own into a Bedrock add-on,
  locally on this PC. Nothing is uploaded anywhere.

.EXAMPLE
  .\Port-Mod.ps1 -Jar "C:\Mods\from_the_caves-0.5.6b6-forge-1.20.1.jar"

  Output: C:\BedrockModding\Ports\<mod>\  (RP + BP folders, PORT_REPORT.md, <mod>-PRIVATE.mcaddon)
  Respect the original mod's licence: an "All Rights Reserved" mod's port is for your own testing only.
#>
param(
  [Parameter(Mandatory = $true)][string]$Jar,
  [string]$OutRoot = "C:\BedrockModding\Ports"
)
$ErrorActionPreference = "Stop"
$here = Split-Path -Parent $MyInvocation.MyCommand.Path
function Need($exe, $hint) { if (-not (Get-Command $exe -ErrorAction SilentlyContinue)) { Write-Host "Missing $exe. $hint" -ForegroundColor Red; Read-Host "Enter to close"; exit 1 } }
Need "python" "Install Python 3.11+ from python.org (tick 'Add to PATH')."
Need "javap"  "Install a JDK (Temurin 21 from adoptium.net). javap ships with it."

if (-not (Test-Path $Jar)) { Write-Host "Jar not found: $Jar" -ForegroundColor Red; exit 1 }
python -m pip install --quiet --disable-pip-version-check pillow | Out-Null

$name = [IO.Path]::GetFileNameWithoutExtension($Jar)
$out  = Join-Path $OutRoot $name
New-Item -ItemType Directory -Force -Path $out | Out-Null

Write-Host "1/3 Reading the mod (bytecode, no code is run)..." -ForegroundColor Cyan
python -I "$here\extract.py" $Jar -o $out
Write-Host "2/3 Generating Bedrock packs..." -ForegroundColor Cyan
python -I "$here\generate.py" $Jar "$out\entities.json" -o "$out\build" --private
Write-Host "3/3 Validating..." -ForegroundColor Cyan
python -I "$here\validate.py" "$out\build"

# package: .mcpack per pack inside one .mcaddon (double-click to import into Minecraft)
$build = Join-Path $out "build"
$tmp = Join-Path $out "pkg"; Remove-Item $tmp -Recurse -Force -ErrorAction SilentlyContinue; New-Item -ItemType Directory $tmp | Out-Null
foreach ($p in Get-ChildItem $build -Directory) {
  Compress-Archive -Path "$($p.FullName)\*" -DestinationPath (Join-Path $tmp "$($p.Name).zip")
  Rename-Item (Join-Path $tmp "$($p.Name).zip") "$($p.Name).mcpack"
}
$addon = Join-Path $out "$name-PRIVATE.mcaddon"
Remove-Item $addon -ErrorAction SilentlyContinue
Compress-Archive -Path "$tmp\*" -DestinationPath "$addon.zip"; Rename-Item "$addon.zip" (Split-Path $addon -Leaf)
Write-Host "`nDone: $addon" -ForegroundColor Green
Write-Host "Report: $build\PORT_REPORT.md"
Read-Host "Enter to close"
