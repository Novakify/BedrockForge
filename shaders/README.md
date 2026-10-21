# Bedrock Forge shaderpacks

`src/<Material>/*.hlsl` = our original shader sources. `packs/<Pack>/renderer/materials/*.material.bin` = built packs.

Install: copy `packs/<Pack>` to `<instance>/mods/BedrockForgeCore/shaderpacks/<Pack>` and put the pack name in
`mods/BedrockForgeCore/config/shaderpack.txt` (`off` = vanilla). Restart the game (materials load at startup).

Build: `python tools/ShaderForge/build.py <game>/data/renderer/materials shaders/src/Anime -o shaders/packs/Anime`.
It generates each variant's input struct from the vanilla signature and only accepts a blob whose compiled signature
matches vanilla (otherwise that variant stays vanilla). Under the hood: `tools/ShaderForge/lz.py` (lazurite + 1.26 buffer-layout fix, byte-identical round trip verified on
Sky, RenderChunk, ColorPostProcessing) + Microsoft dxc for SM6.0 / SM6.5 blobs. SM4/5 blobs stay vanilla (D3D11 fallback).

| Pack | Material | Effect | Status |
|---|---|---|---|
| Anime | Sky | cel-banded sky gradient + horizon glow | 4/4 SM6 blobs, signature + bindings match vanilla |
| Anime | RenderChunk | cel (banded) lighting with cool shadows, +15% saturation; Opaque / AlphaTest / Transparent | 48/60 blobs (Seasons variants stay vanilla) |
