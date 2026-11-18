# Shine: VFX (tapeQ) -> Bedrock feasibility (2026-10-21)

Source looked at: public repo github.com/tapeQz/Shine (main = 1.0.0, Fabric 1.21.11) + its release jar, analysed with
JavaModAnalyzer. The CurseForge file Fernando linked (26.x, >30 MB) is newer and adds atmosphere/weather/particle effects.
**Licence: All Rights Reserved / proprietary** ("may not modify ... without prior written permission"; 1.0.0's
fabric.mod.json said PolyForm Shield). So: NO code, shaders or textures from Shine are copied or translated. Bedrock
versions of the *effects* are written from scratch (ideas/techniques aren't copyrightable). If Fernando wants a faithful
port, ask tapeQ for written permission first (Discord linked in their README).

## What Shine does (1.0.0) and how
| Effect | Java technique (summary, no code) | Bedrock route |
|---|---|---|
| Selective bloom | per-block bloom strength written into terrain vertex data via chunk-meshing mixins, then extract -> downsample/upsample blur chain -> composite (post chain) | A) Vibrant Visuals: native bloom + emissive (MER) textures - resource pack only. B) Fancy: ShaderForge pack - RenderChunk marks emissive pixels (block-light UV + texture brightness), ColorPostProcessing does a single-pass wide-kernel bloom (RenderDragon has no custom post passes without frame-graph hooks) |
| Coloured lighting | light colour per source in the light pipeline | A) VV point lights (native in 1.26) + per-block light colour in a VV pack. B) Fancy: tint block light by hue heuristics only (Bedrock's lightmap has no per-source colour) - partial |
| Rim lighting (block outlines) | edge term in terrain fragment shader | ShaderForge RenderChunk: edge factor from block-face UV / derivatives - fully doable |
| Disabled shading (cartoon look) | removes directional face shade | ShaderForge RenderChunk: divide out the baked face shade using the face normal from ddx/ddy - doable |
| Config screen | YACL | Renderer Foundation overlay (F6) / Bedrock Forge ClickGUI toggles + per-effect strength |

JavaModAnalyzer score 0.0 ("hard") because every part is GL/Blaze3D + 12 render mixins: there is nothing to port 1:1, it is
a re-implementation in RenderDragon shaders. Verdict: **possible** - bloom/rim/no-shading as an original "Shine-style"
shaderpack (ShaderForge, needs the PC's vanilla materials to build), coloured light best via Vibrant Visuals.

Plan: 1) VV resource-pack variant (no native code) 2) Fancy shaderpack variant (RenderChunk + ColorPostProcessing) served by
Renderer Foundation 3) toggles in the overlay. Status: Theoretical - nothing built until PC access.
