# Shine 3.0 -> Bedrock conversion matrix

Source: shine-3.0.jar (CurseForge file 8659331, 103,505,462 bytes, sha1 ce8bfae3feda3e6765d315779b8940ac957c3c44), Fabric 1.21.11.
Feature list = the mod's own assets/shine/api/feature_catalog.json (133 leaf features).

Routes: **S** = RenderDragon shader (ShaderForge, served by Renderer Foundation) - S(v) needs vertex shaders, S(post) the
colour/post pass, S(sky) the Sky/Cloud materials; **VV** = native Vibrant Visuals equivalent; **P** = Bedrock particle
(converted by tools/convert_assets.py); **A** = Bedrock sound; **N** = native client logic (Bedrock Forge-style spawner /
hooks: decides where/when to spawn, reads config); **RP** = plain resource-pack data.

| Category | Feature | Route |
|---|---|---|
| lighting | Bloom Masks | S |
| lighting | Rim Light | S |
| camera | Experimental Full-Screen Post Processing | S(post) |
| lighting | Vanilla Shading Removal | S |
| lighting | Colored Lighting | S+N |
| lighting | Water Reflections | VV/S |
| lighting | World Shadow | VV |
| lighting | Canopy Shade | S |
| lighting | Plant Contact Shadows | S |
| lighting | God Rays | VV/S |
| lighting | Sky Volumetric Lighting | VV |
| lighting | Chunks Fade | S |
| lighting | Block Motion | S(v) |
| world | Biome Colors | S/RP |
| world | Leaf Block Colors | S/RP |
| world | Biome Noise | S(sky/post) |
| world | Biome Visuals | S/RP |
| world | Biome Fog | S(sky/post) |
| world | Foliage Wind | S(v)/N |
| world | Grass Blades | S(v)/N |
| world | Shore Foam | S |
| world | Water Caustics | S |
| world | Underwater Polish | S |
| world | Lava Noise | S(sky/post) |
| world | Heat Haze | S(sky/post) |
| world | Layered Atmosphere | S(sky/post) |
| world | Cold Refraction | S(sky/post) |
| world | Chromatic Refraction | S(sky/post) |
| world | Psychedelic Refraction | S(sky/post) |
| world | Psychedelic Afterimage | S(sky/post) |
| world | Haze | S(sky/post) |
| world | Cloud Veil | S(sky/post) |
| world | Shooting Stars | S(sky/post) |
| world | Main Cloud Layer | S(sky/post) |
| world | Second Cloud Layer | S(sky/post) |
| world | Third Cloud Layer | S(sky/post) |
| world | Vanilla Stars | S(sky/post) |
| world | Day Stars | S(sky/post) |
| world | Custom Stars | S(sky/post) |
| world | Night Noise | S(sky/post) |
| world | End Sky Noise | S(sky/post) |
| particles | Fog FX | P+N |
| particles | Wind Particles | P+N |
| particles | Passive Leaves | P+N |
| particles | Falling Leaves | P+N |
| particles | Leaf Gusts | P+N |
| particles | Jellyfish | P+N |
| particles | XP Orb Trails | P+N |
| particles | Cave Dust | P+N |
| particles | Amethyst Sparkles | P+N |
| particles | End Dust | P+N |
| particles | Nether Lava Dust | P+N |
| particles | Nether Rays | P+N |
| particles | Sculk Dust | P+N |
| particles | Custom Particles | P+N |
| particles | Lantern Particles | P+N |
| particles | Torch Particles | P+N |
| particles | Wall-Torch Particles | P+N |
| particles | Modern Torch Smoke | P+N |
| particles | End Portal Eye Placement | P+N |
| particles | End Portal Rising Burst | P+N |
| particles | End Portal Light Rays | P+N |
| particles | End Portal Fireflies | P+N |
| particles | Ground Mist | P+N |
| particles | Desert Dust Particles | P+N |
| particles | Lava Steam | P+N |
| particles | Lava Embers | P+N |
| particles | Big Lava Pops | P+N |
| particles | Small Lava Pops | P+N |
| particles | Lava Spray | P+N |
| particles | Lava Droplets | P+N |
| particles | Glowing Ash | P+N |
| particles | Pale Garden Dust | P+N |
| particles | Pale Garden Eyes | P+N |
| particles | Ambient Light Rays | P+N |
| particles | Distant Light Rays | P+N |
| particles | Lens Flare | P+N |
| particles | Wind Gusts | P+N |
| particles | Pollen | P+N |
| particles | Fireflies | P+N |
| particles | Birds | P+N |
| particles | Vultures | P+N |
| particles | Butterflies | P+N |
| particles | Geese | P+N |
| particles | Tumbleweeds | P+N |
| interaction | General Interaction | P+N |
| interaction | Foliage Sway | P+N |
| interaction | Surface Imprints | P+N |
| interaction | Leaf Litter | P+N |
| interaction | Flower Litter | P+N |
| interaction | Petal Litter | P+N |
| interaction | Branch Litter | P+N |
| interaction | Pebbles | P+N |
| interaction | Beach Shells | P+N |
| interaction | Lava Plates | P+N |
| interaction | Water Cascades | P+N |
| interaction | Chest Bubbles | P+N |
| interaction | Boat Trail Splashes | P+N |
| interaction | Boat Side Waves | P+N |
| interaction | Water Splash Impact | P+N |
| interaction | Water Trailer Splash | P+N |
| interaction | Lily Pads | P+N |
| interaction | Duckweed | P+N |
| interaction | Algae Flecks | P+N |
| weather | Block-Side Rain | P+N/S |
| weather | Rain Puddles | P+N/S |
| weather | Nether Storm | P+N/S |
| weather | Lightning Flash | P+N/S |
| weather | Wet Weather | P+N/S |
| weather | Rain Biome Visuals | P+N/S |
| weather | Thunder Biome Visuals | P+N/S |
| weather | Nether-Storm Biome Visuals | P+N/S |
| weather | Light-Rain Biome Visuals | P+N/S |
| weather | Weather Snow Particles | P+N/S |
| weather | Weather Dust Particles | P+N/S |
| weather | Passive Snow | P+N/S |
| weather | Clearing Rain | P+N/S |
| weather | Rainbow | P+N/S |
| weather | Aurora | P+N/S |
| camera | Depth of Field | S(post) |
| camera | Vignette | S(post) |
| camera | Camera Noise | S(post) |
| camera | Camera Shake | N |
| camera | Low-Light Desaturation | S(post) |
| camera | Adaptive Palette | S(post) |
| camera | Motion Blur | S(post) |
| camera | Color Grading | S(post)/VV |
| sound | Bedrock Jump Sounds | A+N |
| sound | Grass Wind Sounds | A+N |
| sound | Deep Dark Sounds | A+N |
| sound | Firefly Sounds | A+N |
| sound | Cicada Sounds | A+N |
| custom | Other Shine Effects | ? |

Counts: ?: 1, A+N: 5, N: 1, P+N: 63, P+N/S: 15, S: 9, S(post): 7, S(post)/VV: 1, S(sky/post): 20, S(v): 1, S(v)/N: 2, S+N: 1, S/RP: 3, VV: 2, VV/S: 2

## Phases
1. Assets -> RP (done: 81 particles atlased, 49 sound events, 34 textures; vanilla-sprite particles to map onto Bedrock's
   own cherry/smoke/bubble particles).
2. Particle behaviour + spawn rules (lifetime, motion, where/when). Shine's values live partly in defaults/*.json (read
   directly); the rest is in its compiled Java - need tapeQ's source or an OK to read the bytecode (see below).
3. Shaders: bloom/rim/no-shading/colour grade/vignette first (RenderChunk + post), then sky/clouds/water, then vertex
   effects (wind, block motion) - needs the PC's vanilla materials for ShaderForge.
4. Config: Shine's presets/toggles -> Renderer Foundation overlay.

Open item: decompiling the jar was blocked in the cloud session; ask tapeQ for the 3.0 source (1.0.0 is public).
