# VVPackGen - per-shaderpack Vibrant Visuals resource packs

Bedrock Forge shaderpacks replace VV's final pass (`ColorPostProcessing`, see `shaders/src/_shared/ColorPostVV.fragment.hlsli`),
but VV's own sky scattering, volumetric fog and sun/sky light come from resource-pack JSON. Left vanilla they wash every pack
out (bright white haze). This tool builds one VV resource pack per shaderpack so VV matches that pack's Fancy look.

```
python3 build.py <instance dir> <development_resource_packs dir> Hysteria Insanity Anime Dread CursedFog
```

- Reads vanilla VV JSON straight from the game (`data/resource_packs/vanilla_*/__brarchive/*.brarchive`, newest version wins).
  Mojang files - never committed; the generated packs are not committed either (regenerate them).
- Writes `atmospherics/`, `lighting/`, `fogs/`, `biomes/*.client_biome.json`, `shadows/global.json`, `manifest.json` (capabilities `pbr`).
- Tuning lives in `profiles.json` (multipliers on vanilla values + absolute fog numbers). Atmospherics/lighting reuse the vanilla
  `minecraft:` identifiers (allowed); fogs may NOT use `minecraft:` (content log: "minecraft namespace cannot be used"), so they
  are renamed `bfvv_<pack>:fog_*` and every client biome is pointed at them.
- Volumetric fog: `weather` is valid only under `density`, not under `media_coefficients` / `henyey_greenstein_g` (schema error
  invalidates the whole file).
- Tune loop: edit profiles.json -> build -> Save & Quit + rejoin the world (resource packs reload on rejoin; shaders don't).
  Check `<instance>/Minecraft Bedrock/logs/ContentLog*.txt` for `[Rendering][error]` lines after each rejoin.
- Each pack is a separate RP (HysteriaVVRP, InsanityVVRP, ...). Activate the one matching shaderpack.txt in the world's resource packs.
- Supersedes tools/HysteriaVV (lighting + shadows only); HysteriaVVRP keeps its UUID so worlds that have it stay linked.
- NEVER set a VV RP as a GLOBAL resource pack: global packs sit above the world's packs and broke the Fancy impact frame
  (2026-10-28). Activate it per world, with "Bedrock Forge Impact" listed first (above it).
- The look also needs the shader side: each pack's `ColorPostProcessing/Fragment.hlsl` sets `VV_EXPOSURE_BIAS` (+ `VV_DEHAZE`,
  `VV_SATURATION`, `VV_WHITE_BALANCE`). At the default 1.0 every pack blew out to near-white on VV. Current: Hysteria 0.32,
  Insanity 0.12, Anime 0.15, Dread 0.2, CursedFog 0.16.
