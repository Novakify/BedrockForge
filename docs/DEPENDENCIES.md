# Dependencies (Phase 1 only)

Required to build + run a LeviLamina client mod:

| Tool | Why | Source | Admin? |
|---|---|---|---|
| Git | clones | git-scm.com / `winget install Git.Git` | installer asks |
| Visual Studio 2022 Build Tools — "Desktop development with C++" (MSVC + Windows SDK) | headers/libs for clang-cl | visualstudio.microsoft.com | Yes |
| LLVM (clang-cl) | template toolchain | `winget install LLVM.LLVM` | Yes |
| xmake | build system | `winget install Xmake-io.Xmake` | No |
| VC++ Redistributable x64 | LeviLamina runtime | LeviLauncher offers it | Yes |
| LeviLauncher + lip | instances + package install | GitHub releases | No |
| Gaming Services + GameInput | GDK Minecraft | Microsoft Store / already present if MC runs | — |

**Not needed yet:** Python, Node, CMake, JDK, GitHub CLI. JDK comes in at Task 10 (Java mod analysis).
