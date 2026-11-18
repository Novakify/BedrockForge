# Porting feasibility: HitFX (synthetic test mod)

- **Loader:** fabric  |  **id:** hitfx  |  **version:** 0.0.1
- **License:** CC0-1.0  (check before reusing any code or assets)
- **Environment:** client  |  **classes:** 3  |  **mixins:** 1
- **Score:** 0.6 → **Portable with native work**

> Static analysis only. Labels: *Theoretical* until a port compiles and runs in game.

## Dependencies

- `fabricloader` >=0.16
- `fabric-api` *
- `minecraft` 1.21.x

## What it touches → Bedrock Forge mapping

| Area | Refs | Bedrock path | Examples |
|---|---|---|---|
| Rendering (Blaze3D/GL) | 2 | native-only: needs RenderingAPI (not built yet) | `RenderSystem` |
| Keybinds | 2 | supported: bf::input::bindKey | `KeyBindingHelper`, `KeyBinding` |
| Entities | 1 | pack: custom entities (behavior + resource pack) | `LivingEntity` |
| UI / HUD / screens | 1 | partial: bf::hud (v0) covers HUD text/boxes | `DrawContext` |

## Mixins (engine patches: each one needs a native hook or a redesign)

- [client] `LivingEntityMixin` → `LivingEntity`

## Assets (candidates for AssetConverter, license permitting)

- assets/hitfx/sounds (.ogg): 1
- assets/hitfx/lang (.json): 1
- assets/hitfx/textures (.png): 1
