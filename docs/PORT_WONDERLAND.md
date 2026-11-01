# The Wonderland 3.0.3 (Forge 1.20.1, MCreator + GeckoLib) -> Bedrock - port status

Private personal use only. The mod file came from its creator for this conversion (Fernando, 2026-10-31);
credits stay in the packs (CREDITS.txt + mods.toml authors). Ask the creator before sharing anything.

Build: `python3 -I tools/GeckoBridge/build.py the_wonderland-3.0.3-1.20.1-forge-MIT-Novak.jar OUT --name Wonderland --tol 0.25`
(~14 min, cached discovery afterwards). Overrides: `tools/GeckoBridge/mods/the_wonderland.json`.

| Part of the mod | Status in Bedrock | How |
|---|---|---|
| 95 GeckoLib mobs | works | GeckoBridge M1/M2 (models, baked animations, controllers, triggers) |
| 34 non-GeckoLib mobs (The Eye, Hoarder, Sorrow, Eyeless, Somebody, Spawn Cow/Pig, projectiles...) | works | `vanilla.py`: Java model bytecode -> geometry, setupAnim -> Molang, eye layers -> glow |
| Mob AI | approximate | Java `registerGoals` -> Bedrock behaviours; procedures drive the rest |
| Procedures, events, effects, items, blocks hooks (MCreator Java) | works (translated) | JavaBridge: bytecode -> Script API JS, 72 event hooks |
| Jumpscare / screen overlays (15) | works | HUD JSON UI images keyed by formatting-code titles |
| 44 dimensions + structures | works (structure tiles in a void) | ModPorter dimensions.py + structures.py, script world generator, fog, ambience, portals |
| Blocks (87), items, recipes, loot, lang | works | ModPorter content.py |
| Mod sounds | works | all mod sound events as `the_wonderland.<event>` |
| Delayed procedure steps (`queueServerWork`) | works | Bedrock scheduler (was silently never run before 2026-10-31 19:40) |
| Chat / title text built by string concatenation | works | javap escapes decoded (before: `\u0001` instead of the value) |
| `/wonderland ...` debug command (58 paths) | works as `/the_wonderland:wonderland <args>` | Brigadier tree -> Bedrock custom command |
| Blood particles (3) | works | Bedrock particle JSON from the mod's texture + Java particle constants |
| Spawn eggs | real colours | ForgeSpawnEggItem colours |
| Vanilla retextures (blocks, items, mobs, armor, HUD icons, colormaps) | works where the layout matches | only files Bedrock has at the same path + size (GeyserMC mapping + bedrock-samples); others skipped |
| Vanilla sound replacements | works | same path in Bedrock |
| Splash texts | works | splashes.json |
| Java shader (only `fog.glsl`: fog starts at 20% distance) | works | all 80 Bedrock fog definitions overridden with fog_start x 0.2 |
| 18 painting variants | works (as wall entities) | item per painting hangs it on the clicked face; punching drops it back (Bedrock can't add painting variants) |
| Title screen logo / panorama | not portable | Bedrock's menu is OreUI with a 3D world backdrop, not these textures |
| GUI screens | n/a | Wonderland has none besides the overlays (all converted) |

Checked in game (2026-10-31 evening, "GeckoBridge Test" world, ~60 fps with all 129 mobs summoned):
packs load, ContentLog has no Wonderland errors, mod chat lines / jumpscares / camera effects fire, Hellways
rooms generate, retextured cow/zombie render correctly.

Fixed on the way: delayed work never ran; concatenated strings broken; Java particles fell back to smoke; whole-set vanilla retextures crashed world load; eye overlay stuck (typed entity query matched
the player); spawn-search loop (getBlockFloorHeight stub); Java block ids in `fill` commands (snow_block -> snow);
EntityType.spawn overload picked the wrong argument as position.
