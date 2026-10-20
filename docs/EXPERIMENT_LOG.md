# Experiment Log

Format: `E-### | date | commit | environment | what was done | what was seen | label`

E-001 | 2026-10-20 | — | cloud workspace | Cloned 6 upstream repos, read manifests/licenses/READMEs | Target set to 1.26.51.01 + LeviLamina 26.51.x | Verified in source
E-002 | 2026-10-20 | 9227715 | cloud | Wrote BedrockForgeCore (SDK v0, P1+P2) | Not compiled yet | Theoretical
E-003 | 2026-10-20 | - | cloud | JavaModAnalyzer v0 on synthetic Fabric jar (keybind+HUD+Blaze3D+1 mixin) | Correct buckets, score 0.6 "Portable with native work"; fixed own-package + asset-grouping bugs | Verified by execution
E-004 | 2026-10-20 | - | cloud | Wrote Prototype 3 ImpactFrames (ActorHurtEvent → atomics → AfterUIRenderEvent overlay + playUI) | Not compiled yet | Theoretical
E-005 | 2026-10-20 | - | cloud | Analyzed user's impact-frames-1.2.1.5.jar (Fabric, MC 26.2, 44 classes, 18 mixins, ARR) | Score 0.14: mostly rendering mixins. Reworked P3 to alternating-frame model + trigger rules. Fixed analyzer chat-component misclassification | Verified by execution (analyzer); P3 Theoretical
E-006 | 2026-10-20 | 21e992e/5ff7389 | GitHub Actions windows-latest | First CI builds | Both green; DLL 569 KB x64 PE. Found manifest platform '${modPlatform}' (would be rejected by ModLoadPlanner) -> fixed in 9ed54e5 + patched in v0.1 package | Verified by compilation
