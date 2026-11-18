# Next Steps

## Fernando, at the PC (~25 min)
1. Event Viewer → Windows Logs → System around 3:30 PM: Kernel-Power 41 / 1074 / BugCheck?
2. Install from Downloads\BedrockForge-Installers: Git, xmake, LeviLauncher. Download + install VS 2022 Build Tools (C++) and LLVM 22.1.8 (only needed for local builds; CI already builds).
3. Run Setup-BedrockForge.ps1 (Desktop).
4. LeviLauncher: install lip → new **isolated** instance 1.26.51.01 → Mods → Install LeviLamina.
5. Get the latest build (Claude puts it in Downloads), run `Install-BedrockForge.ps1 -Source <folder>`.
6. New world → activate "Bedrock Forge Impact (Behavior)" → Vibrant Visuals ON → hit a mob. Record a clip.
7. Attach C:\BedrockModding to the Claude chat.

## Claude, next
- Read logs/clip, fix whatever breaks.
- R1: list `data/renderer/materials` of 1.26.51 for the silhouette shader.
- In-game settings screen for impact frames (instead of editing config.json).
- Prototype 4 (horror encounter) after P3 is verified in game.
