# Bedrock Forge shaderpacks

`src/<Material>/*.hlsl` = our original shader sources. `packs/<Pack>/renderer/materials/*.material.bin` = built packs.

Install: copy `packs/<Pack>` to `<instance>/mods/BedrockForgeCore/shaderpacks/<Pack>` and put the pack name in
`mods/BedrockForgeCore/config/shaderpack.txt` (`off` = vanilla). Restart the game (materials load at startup).

Build (cloud or PC): `tools/ShaderForge/lz.py` (lazurite + 1.26 buffer-layout fix, byte-identical round trip verified on
Sky, RenderChunk, ColorPostProcessing) + Microsoft dxc for SM6.0 / SM6.5 blobs. SM4/5 blobs stay vanilla (D3D11 fallback).

| Pack | Material | Effect | Status |
|---|---|---|---|
| Anime | Sky | cel-banded sky gradient + horizon glow | built, signature-matched to vanilla; not yet seen in game |
