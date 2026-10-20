# Next Steps

## You (phone, 1 min)
1. Create an empty **private** GitHub repo named `BedrockForge` under Novakify (no README). Tell Claude; it will push and CI builds the DLL.

## You (at the PC, ~20 min, admin prompts)
1. Install from `Downloads\BedrockForge-Installers`: Git → xmake → LeviLauncher.
2. Download + install: VS 2022 Build Tools ("Desktop development with C++") from visualstudio.microsoft.com, and LLVM 22.1.8 (github.com/llvm/llvm-project/releases/tag/llvmorg-22.1.8).
3. Run `Setup-BedrockForge.ps1` on the Desktop (right-click → Run with PowerShell).
4. LeviLauncher: install lip → install MC **1.26.51.01** as a NEW instance → Manage Mods → Install LeviLamina.
5. Attach `C:\BedrockModding` to the Claude chat.

## Claude, after that
- Read env-report, back up com.mojang, drop BedrockForgeCore into the test instance, guide the in-game test (log lines, J toggle, FPS).
- Prototype 3: impact frame (flash + shake + sound) on ActorHurtEvent.
