# World FX "Cloud Sea" — art bible (standalone Hive look)

The standalone look: no music, no LyricsForge. A Hive Bed Wars / Sky Wars map floats above a sea of cloud at the last
minutes of sunset. Everything on screen is lit by that one moment. Standard: docs/VISUAL_STANDARD.md.

## The light (everything follows from this)
- **Key**: low sun, about 6.5° up, warm (1.00, 0.34, 0.11 linear). Long shadows, about one block per block of height.
- **Fill**: violet sky light from above (0.30, 0.29, 0.46). Shadows are violet, never grey or black.
- **Bounce**: the cloud sea is lit pink and orange, so faces turned down and away pick up a faint warm lift from below.
- **Rim**: edges against open sky on the sun side catch a thin warm line, and only those edges.
- **Air**: distance fades into the real sky and cloud background, never into a flat fog colour.
- Why the references work: two complementary temperatures (orange key, violet fill), a value range that keeps real
  darks, and detail that lives in the light and shadow shapes rather than in noise.

## Materials: each one tells the truth about what it is
| material | look | never |
|---|---|---|
| wool | velvet: soft wrap, sheen at grazing angles, no specular | glossy, noisy |
| concrete | clean, dense colour, a faint satin highlight, crisp edges | plastic, mirror |
| planks / log | grain relief follows the grain, warm translucency at edges | uniform bump |
| stone / cobble | deep cavities, chipped-edge highlights | smeared |
| end stone | porous, pale, soft cavities | glossy |
| obsidian | black glass: sharp, small, cool reflections with a violet sheen | matte |
| metals | colour lives in the reflection; diffuse is low | pink plastic, neon gold |
| gems / ores | sparkle only on the gem texels | a sparkle on the stone around them |
| glass / water | fresnel sky reflection; you see through them more straight on | opaque mirror |

## Equipment: "forged at golden hour"
Equipment uses the same light as the blocks, so it belongs to the world, but each tier must read at a glance in PvP:
**colour first, silhouette second, finish third.** Hues stay true to vanilla (opponents read tiers by colour at range).
- **Wood**: pale ash. Real grain along the blade, rounded worn edges, a dark wrapped grip.
- **Stone**: a knapped flint blade with facet planes, lashed to a wooden grip with cord.
- **Iron**: brushed steel. A fuller down the blade, a bevelled bright edge line, a leather grip, a plain cross guard.
- **Gold**: cast ceremonial gold. A softer, wider blade with a curled guard, warm and burnished rather than neon.
- **Diamond**: a cut-crystal blade (hard facets, light and dark planes, a bright edge line) set in a silver guard.
- **Netherite**: blackened forged steel with a hammered surface and a broad blade; restrained, no glow.
- **Enchanted**: not the vanilla purple wash. A slow pale light runs along the edge (to be designed).
- **Readability rules**:
  - The blade stays the brightest, most saturated part.
  - The silhouette stays recognisably a sword, axe or pickaxe.
  - First person must not cover more screen than vanilla.

## Review loop (every piece)
1. Study the reference and write down why it works.
2. Build it.
3. Look at real in-game frames: first person, third person, near and far, and in motion.
4. Do 2–3 art-director reviews. Name what fails, fix it, look again.
5. Write down open limits honestly in the hand-off.
