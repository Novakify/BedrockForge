# Shine for Bedrock (conversion) - PRIVATE

Bedrock (1.26, RenderDragon) conversion of **Shine: VFX by tapeQ**. Shine is tapeQ's work - nothing in this
branch is ours to claim; we only convert it to Bedrock, with the author's permission for Shine 3.0
(see `PERMISSION.md`). Do not publish or redistribute without checking that permission's terms.

Original: https://www.curseforge.com/minecraft/mc-mods/shine-vfx  |  https://github.com/tapeQz/Shine
Licence of the original: All Rights Reserved (c) 2026 tapeQ.

Layout (planned):
- `shaders/` - Shine's GLSL effects converted to HLSL for ShaderForge (RenderChunk + post), built against the game's
  vanilla materials, served by Renderer Foundation / ForgeRender.
- `vv/` - Vibrant Visuals resource-pack variant where Bedrock has a native equivalent (bloom, emissive, point lights).
- `config/` - Shine's options mapped to overlay / ClickGUI toggles.
