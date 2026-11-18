# Project Bedrock Forge - Phase 1 bootstrap
# 1) Creates the C:\BedrockModding folder tree (folders only, never deletes or overwrites anything)
# 2) Runs a READ-ONLY inspection of your PC and saves C:\BedrockModding\Logs\env-report.txt
# Run: right-click this file > Run with PowerShell
$root = 'C:\BedrockModding'
$dirs = 'Backups','Downloads','Research\LeviLamina','Research\Lamium','Research\LeviLauncher','Research\Amethyst','Research\BetterRenderDragon','Research\Blueberry','Research\JavaModding',
 'Source\Upstream','Source\BedrockForge\SDK\Core','Source\BedrockForge\SDK\Client','Source\BedrockForge\SDK\Rendering','Source\BedrockForge\SDK\Audio','Source\BedrockForge\SDK\Camera','Source\BedrockForge\SDK\World','Source\BedrockForge\SDK\Compatibility',
 'Source\BedrockForge\Tools\JavaModAnalyzer','Source\BedrockForge\Tools\ModPorter','Source\BedrockForge\Tools\AssetConverter',
 'Source\BedrockForge\Examples\HelloWorld','Source\BedrockForge\Examples\CustomHUD','Source\BedrockForge\Examples\ImpactFrames','Source\BedrockForge\Examples\HorrorTest',
 'TestMods','TestWorlds','Logs','Documentation','Tests'
foreach ($d in $dirs) { New-Item -ItemType Directory -Force -Path (Join-Path $root $d) | Out-Null }
Write-Host "Workspace ready at $root"

$out = Join-Path $root 'Logs\env-report.txt'
$r = @()
$r += "== OS"; $r += (Get-CimInstance Win32_OperatingSystem | Select-Object Caption, Version, OSArchitecture | Out-String)
$r += "== GPU"; $r += (Get-CimInstance Win32_VideoController | Select-Object Name, DriverVersion | Out-String)
$r += "== Tools"
foreach ($t in 'git','python','node','cmake','clang-cl','xmake','gh','java','javac','lip','codex','claude','winget') {
  $c = Get-Command $t -ErrorAction SilentlyContinue
  if ($c) { $v = try { (& $t --version 2>&1 | Select-Object -First 1) } catch { '?' }; $r += "{0,-9} {1}  [{2}]" -f $t, $v, $c.Source }
  else { $r += "{0,-9} MISSING" -f $t }
}
$r += "== Visual Studio / MSVC"
$vsw = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
if (Test-Path $vsw) { $r += (& $vsw -all -products * -format text | Select-String 'displayName|installationVersion|installationPath') } else { $r += 'vswhere not found (no VS installed)' }
$r += "== Windows SDKs"; $r += (Get-ChildItem "${env:ProgramFiles(x86)}\Windows Kits\10\Include" -ErrorAction SilentlyContinue | Select-Object -Expand Name)
$r += "== Minecraft packages"; $r += (Get-AppxPackage *Minecraft* | Select-Object Name, Version, InstallLocation | Out-String)
$r += "== Gaming Services / GameInput"; $r += (Get-AppxPackage *GamingServices* | Select-Object Name, Version | Out-String)
$r += (Get-Service GameInputSvc -ErrorAction SilentlyContinue | Select-Object Name, Status | Out-String)
$r += "== com.mojang locations"
foreach ($p in "$env:APPDATA\Minecraft Bedrock", "$env:LOCALAPPDATA\Packages\Microsoft.MinecraftUWP_8wekyb3d8bbwe\LocalState\games\com.mojang") {
  if (Test-Path $p) { $r += "$p"; $w = Get-ChildItem $p -Recurse -Directory -Filter minecraftWorlds -ErrorAction SilentlyContinue | Select-Object -First 3; foreach ($x in $w) { $r += "  worlds: $($x.FullName) ($((Get-ChildItem $x.FullName -Directory).Count))" } }
}
$r += "== LeviLauncher"; $r += (Get-ChildItem "$env:LOCALAPPDATA","$env:APPDATA","$env:ProgramFiles" -Directory -Filter '*Levi*' -ErrorAction SilentlyContinue | Select-Object -Expand FullName)
$r += "== VC++ redist x64"; $r += (Get-ItemProperty 'HKLM:\SOFTWARE\Microsoft\VisualStudio\14.0\VC\Runtimes\x64' -ErrorAction SilentlyContinue | Select-Object Version | Out-String)
$r += "== Free disk C:"; $r += ('{0:N1} GB' -f ((Get-PSDrive C).Free/1GB))
$r | Out-File $out -Encoding utf8
Write-Host "Report written to $out  -- now attach C:\BedrockModding to the Claude chat."
Read-Host "Press Enter to close"
