<#
.SYNOPSIS
  Installs Bedrock Forge (native mod + packs) into an ISOLATED LeviLauncher test instance.

.DESCRIPTION
  Safe by design:
   - only touches an instance whose version.json says enableIsolation = true (your normal
     Minecraft and its worlds/packs are never touched) unless you pass -AllowShared
   - only writes these folders inside that instance:
       mods\BedrockForgeCore            (your config\ inside it is kept)
       ...\resource_packs\BedrockForgeImpact
       ...\behavior_packs\BedrockForgeImpactBP
   - anything it replaces is MOVED to C:\BedrockModding\Backups\<timestamp>\ first (never deleted)

.EXAMPLE
  .\Install-BedrockForge.ps1 -Source "$HOME\Downloads\BedrockForgeCore-v0.2-build"
  .\Install-BedrockForge.ps1 -Source ... -Instance "BF-Test"
#>
param(
  [Parameter(Mandatory = $true)][string]$Source,   # folder containing BedrockForgeCore\ and packs\
  [string]$Instance = "",                          # LeviLauncher instance (version) name; asks if omitted
  [string]$TargetGameVersion = "1.26.51",
  [switch]$AllowShared                             # allow a NON-isolated instance (not recommended)
)
$ErrorActionPreference = "Stop"

function Fail($msg) { Write-Host "ERROR: $msg" -ForegroundColor Red; Read-Host "Press Enter to close"; exit 1 }

# ---- locate LeviLauncher data root (honours base_root in its config.json) ----
$llRoot = Join-Path $env:APPDATA "levilauncher.exe"
$cfg = Join-Path $llRoot "config.json"
$baseRoot = $llRoot
if (Test-Path $cfg) {
  try { $c = Get-Content $cfg -Raw | ConvertFrom-Json; if ($c.base_root) { $baseRoot = $c.base_root } } catch {}
}
$versionsDir = Join-Path $baseRoot "versions"
if (-not (Test-Path $versionsDir)) { Fail "No LeviLauncher instances found at $versionsDir. Install a version in LeviLauncher first." }

# ---- pick the instance ----
$instances = Get-ChildItem $versionsDir -Directory | ForEach-Object {
  $metaPath = Join-Path $_.FullName "version.json"
  if (Test-Path $metaPath) {
    $m = Get-Content $metaPath -Raw | ConvertFrom-Json
    [pscustomobject]@{ Name = $_.Name; Dir = $_.FullName; Game = $m.gameVersion; Isolated = [bool]$m.enableIsolation; Type = $m.type; Pkg = $m.packageType }
  }
}
if (-not $instances) { Fail "No instances with version.json under $versionsDir." }
Write-Host "`nLeviLauncher instances:" -ForegroundColor Cyan
$instances | Format-Table Name, Game, Isolated, Type, Pkg -AutoSize | Out-Host

if (-not $Instance) {
  $candidates = @($instances | Where-Object { $_.Game -like "$TargetGameVersion*" -and $_.Isolated })
  if ($candidates.Count -eq 1) { $Instance = $candidates[0].Name; Write-Host "Using '$Instance' (only isolated $TargetGameVersion instance)." }
  else { $Instance = Read-Host "Type the instance Name to install into" }
}
$inst = $instances | Where-Object { $_.Name -eq $Instance } | Select-Object -First 1
if (-not $inst) { Fail "Instance '$Instance' not found." }
if (-not ($inst.Game -like "$TargetGameVersion*")) { Write-Host "WARNING: '$Instance' is $($inst.Game), Bedrock Forge targets $TargetGameVersion. The native mod will refuse or misbehave." -ForegroundColor Yellow }
if (-not $inst.Isolated -and -not $AllowShared) { Fail "'$Instance' is NOT isolated, so it shares worlds/packs with your normal Minecraft. Turn on isolation for it in LeviLauncher (or re-run with -AllowShared if you really mean it)." }
if ($inst.Pkg -eq "uwp") { Fail "UWP instances aren't supported by this script yet; use a GDK instance." }

$gameDir  = if ($inst.Type -eq "preview") { "Minecraft Bedrock Preview" } else { "Minecraft Bedrock" }
$comMojang = Join-Path $inst.Dir "$gameDir\Users\Shared\games\com.mojang"
$modsDir   = Join-Path $inst.Dir "mods"
if (-not (Test-Path (Join-Path $modsDir "LeviLamina"))) { Write-Host "WARNING: mods\LeviLamina not found in this instance. Install LeviLamina from LeviLauncher's Mods page first, or the native mod won't load." -ForegroundColor Yellow }

# ---- check the source ----
$srcMod = Join-Path $Source "BedrockForgeCore"
$srcRP  = Join-Path $Source "packs\BedrockForgeImpact"
$srcBP  = Join-Path $Source "packs\BedrockForgeImpactBP"
foreach ($p in $srcMod, $srcRP, $srcBP) { if (-not (Test-Path $p)) { Fail "Missing $p. Point -Source at the unzipped build folder." } }
$man = Get-Content (Join-Path $srcMod "manifest.json") -Raw | ConvertFrom-Json
if ($man.platform -ne "client") { Fail "Mod manifest platform is '$($man.platform)', expected 'client'. Use a newer build." }

# ---- back up whatever we are about to replace ----
$stamp  = Get-Date -Format "yyyyMMdd-HHmmss"
$backup = Join-Path "C:\BedrockModding\Backups" "install-$stamp"
New-Item -ItemType Directory -Force -Path $backup | Out-Null

function Install-Folder($src, $dst, [string[]]$keep = @()) {
  if (Test-Path $dst) {
    $bk = Join-Path $backup (Split-Path $dst -Leaf)
    Move-Item $dst $bk
    Write-Host "  backed up old $(Split-Path $dst -Leaf) -> $bk"
  }
  New-Item -ItemType Directory -Force -Path (Split-Path $dst -Parent) | Out-Null
  Copy-Item $src $dst -Recurse
  foreach ($k in $keep) {   # restore user data (e.g. config\) from the backup
    $old = Join-Path (Join-Path $backup (Split-Path $dst -Leaf)) $k
    if (Test-Path $old) { Copy-Item $old (Join-Path $dst $k) -Recurse -Force; Write-Host "  kept your $k" }
  }
}

Write-Host "`nInstalling into '$Instance'..." -ForegroundColor Cyan
Install-Folder $srcMod (Join-Path $modsDir "BedrockForgeCore") @("config")
Install-Folder $srcRP  (Join-Path $comMojang "resource_packs\BedrockForgeImpact")
Install-Folder $srcBP  (Join-Path $comMojang "behavior_packs\BedrockForgeImpactBP")

@"
Installed Bedrock Forge $($man.version) into $Instance on $stamp
mod:  $modsDir\BedrockForgeCore
rp:   $comMojang\resource_packs\BedrockForgeImpact
bp:   $comMojang\behavior_packs\BedrockForgeImpactBP
"@ | Out-File (Join-Path $backup "INSTALL_LOG.txt") -Encoding utf8

Write-Host "`nDone." -ForegroundColor Green
Write-Host "Next: launch '$Instance' from LeviLauncher -> Create New World (Survival, flat is fine) ->"
Write-Host "      Behavior Packs: activate 'Bedrock Forge Impact (Behavior)' (it pulls in the resource pack) ->"
Write-Host "      Video: turn ON Vibrant Visuals for the black/white world -> hit a mob."
Write-Host "Keys: J = HUD box, K = impact frames on/off. Backups: $backup"
Read-Host "Press Enter to close"
