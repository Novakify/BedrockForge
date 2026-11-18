# RenderingAPI plan: true impact frames (posterized world)

Goal (from Fernando's references): during an impact the **world itself** becomes pure black/white
(hard threshold), and a burst mask (sparkle / focus lines) flips which side is black. Overlays
drawn on top of the colored world can never look like this; it needs a shader pass.

## How custom shaders get into Bedrock (RenderDragon)
Learned by *reading* BetterRenderDragon (AGPL-3.0, concept only, no code copied):

1. **Material redirect.** Shaders ship as compiled `.material.bin` files under
   `<game>/data/renderer/materials/`. Hooking the resource loader lets a `.material.bin` with the
   same name inside a resource pack (`renderer/materials/X.material.bin`) load instead.
2. **Uniform injection.** Hooking `dragon::materials::MaterialUniformMap::setUniform` (vec4
   overload) lets native code override a uniform's value every frame. That's how our mod tells the
   shader "impact = 1, frame = 3, style = sparkle".

## Bedrock Forge implementation steps
| # | Step | Needs | Status |
|---|---|---|---|
| R1 | List the material names in 1.26.51 `data/renderer/materials/`, pick the last full-screen pass (tonemapping / final blit) | Game files on Fernando's PC | Not started |
| R2 | Extract + decompile that material (community MaterialBinTool) and confirm its uniforms | R1 | Not started |
| R3 | Native hook: material-bin redirect (our own implementation over LeviLamina hooks) | LeviLamina memory/hook API | Not started |
| R4 | Native hook: setUniform override, `bf::render::setImpact(amount, frame, style)` | R3 | Not started |
| R5 | Shader: `if (impact > 0)` luminance threshold to B/W, sample burst mask texture, XOR | R2 | Not started |
| R6 | Wire ImpactFrames to drive R4 instead of drawing overlays; keep the overlay path as fallback | R4, R5 | Not started |

Risks: material names/uniforms change between game versions (pin 1.26.51, version-gate the hook).
Vibrant Visuals vs Fancy use different pipelines, so start with Fancy. Single-player/local only.

## Mask art
`tools/gen_impact_v2.py` (original, procedural): `lines` (tapered ink focus lines), `spark` (4-point
sparkle → diamond shockwave), `flare` (cross flare + spiky core). These become the R5 masks.
The reference mod's frames are ARR and are not used.
