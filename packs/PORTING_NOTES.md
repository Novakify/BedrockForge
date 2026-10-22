# Porting notes - From The Fog (Bedrock) v1

Original: From The Fog by LunarEclipseStudios (CC BY-NC-SA 4.0). Section numbers refer to `docs/SPEC.md`.
Status: **done** = behaves as in the spec; **partial** = works with a documented difference; **not ported** = absent.

## Bugs in the original - intended behaviour implemented

| Bug | Decision |
|---|---|
| A1 chance roll typo | Real 1-in-N roll for footsteps, sighting sense (common table), all others default table. |
| A2 `structure_chance` | Per-structure option: common 1/25, default 1/100, rare 1/200. |
| A3-A6, A9 missing files | Skipped (no-ops). |
| A8 nightmare ignores option | `nightmare_sighting` respected; `insane` frequency = always. |
| Malicious Malfunction (client crash) | Replaced by a harmless fake glitch: jumpscare, crash sound, 3 s black camera fade and an "Unknown Entity Error" title. |
| WAKE_UP.vbs | Not ported, by design. |

## Features

| Feature | Status | Notes |
|---|---|---|
| Herobrine entity (§3) | partial | One custom entity with player geometry instead of the item_display rig. No damage, no knockback, not pushable, no collision with players, no gravity, removed by script (never persists across reloads). 62 of 95 skins as `ftf:skin` variants with emissive layer (render-controller `ignore_lighting`), arms/legs/body hidden per skin. Not shipped: skins with custom models, 128 px or animated textures (slenderman, the_man_from_the_fog, werewolf group, white_eyes, glitch, nowhere, distorted, jack_black, freddy_fazbear, lonely, trench_diver, white_pumpkin, cursed_oak, farlander, hoebrine) - they need hand-made Bedrock geometry. Extra accessory parts of some shipped skins (hat_man hat, lumbering_jack axe, slenderman tentacles...) are not modelled. The #FEFEFE glow shader is approximated by the emissive layer. |
| Animations (§3.3) | partial | Breathing 3.2 s, walk 0.7 s, punch, holding pose, head tilt, head look via `q.target_x/y_rotation`. Body-follows-head clamp math uses vanilla body rotation instead of the scripted 45 deg clamp. |
| Held items | partial | `replaceitem` into mainhand + `enable_attachables`; needs in-game check that items render on the custom geometry (`rightItem` bone present). |
| Start rules, 1 s / 5 min loops, activity multiplier (§1.1, §1.6, §1.7) | done | State in world dynamic properties (`ftf:config`, `ftf:state`, `ftf:pos:*`, `ftf:struct:*`). The 5-minute loop is a countdown the multiplier can pull earlier. |
| Sighting scheduler, presets, routing (§2.1, §2.2) | done | Includes window (day/night/rain via weatherChange cache) and door-ghost block. |
| Positioning + ground correction (§2.3) | done | Block-based descent (max ~40 blocks) instead of 0.1-step; heightmap via `getTopmostBlock` skipping leaves. |
| Spot detection (§2.5) | done | View cone from `getViewDirection` with the spec's angle table; line of sight via `getBlockFromRay`, passing through see-through blocks; glass flag for Window Watcher. The Java "spread entity retry" hack is not needed (the entity spawns at once). |
| Vanish, slip behind, sighting noise, shrouded specter, GHC head tilt (§2.6) | done | Specter uses custom particles `ftf:poof/smoke/portal`. |
| Suspenseful sighting (§2.7) | done | `dynamic` slenderman/werewolf stings only trigger for those skins, which are not shipped yet, so it plays `default`. |
| Sudden scare, teleport jumpscare (§2.8) | done | |
| Sneaky strike, punching/shooting Herobrine (§2.9) | done | Hit penalty uses `selfDestruct` damage (2). |
| Nightmare (§2.10) | done | Sleep detected by polling `isSleeping` each second. |
| No Sleep (§2.10) | partial | Herobrine carries the `monster` family while `no_sleep` is on; relies on Bedrock's monsters-nearby sleep rule (verify in game). |
| Dwelling Shadow (§2.11) | done | `getLightLevel`. |
| Sighting Sense (§2.12) | partial | Tamed wolves within 80 turn toward him and growl; Bedrock has no API to set wolf anger target. |
| Haunted Herd (§2.13) | partial | Uses a real pig/cow/frog (biome variant chosen by vanilla) frozen with slowness 255 that stares at the player; when spotted at range it spins 180 deg, within 20/7 blocks it is released as a normal mob tagged for the kill achievement. No separate fake model, no fake shadow. |
| Ghost Miner (§2.14) | done | 100-block tunnel dug with `runJob`, redstone torches, liquid plugging, iron pickaxe, mining sounds. Tunnel is axis-aligned. |
| Fearful Footsteps (§2.15) | done | |
| Sinister Signs (§2.16) | done | Biome wood, 12 messages (local/swedish), player name in message 4; sign is waxed. Herobrine holds the sign item first. |
| Malevolent Shrine (§2.17) | done | Classic + mod layouts; detection via flint and steel / fire charge use. Caution Caption shown as a title subtitle (Bedrock has no custom subtitles). |
| Block-event framework (§2.18) | done | `playerPlaceBlock`; forests logged by biome check once per 5 min per player. Events only run in loaded chunks (no forceload). |
| Burning Base, Disappearing Torches, Crimson Curse (+Soul Shift), Chilled Candles, Leafless Grove, Ghost Doors (§2.19) | done | Leafless Grove clears leaves (non-persistent) and border logs over 3 chained 51x101x51 areas, spread over ticks. Ghost door is an invisible tiny `ftf:door_ghost` that opens doors. |
| Dreadful Donation (§2.19a) | partial | Weights 70/30/90/+5 and names as spec; heirloom roll gives a custom disc item (scripted jukebox playback: plays the song, does not insert/stop like a real record). Heirloom paintings not ported (no custom painting variants in Bedrock). |
| Twisted Tapestries (§2.20) | not ported | Bedrock has no custom painting variants and the Script API cannot read a painting's motif. Textures are converted into the RP (`textures/ftf/paintings/`) for a future overlay-entity approach. Option kept (default off, as in Java). |
| Structures (§2.21, §4) | partial | All 11 templates converted to `.mcstructure`. Instead of worldgen base markers, the script scouts candidate surface sites (48-128 blocks from players, matching biomes) every 30 s, then the 5-minute roll + 5xHx5 collision check places a random template with `structureManager.place`. Feature-rule worldgen anchors not implemented. |
| Config menu (§1.8) | done | `/ftf:config`, `/scriptevent ftf:config`, Fog Journal item. ActionForm main menu with the original icons, ModalForm pages, paged skin library, Super Secret Settings button, per-option toggle sounds, admin lock list (`ftf:locked` world property). The 3D in-world menu, tooltips and wardrobe preview are replaced by forms. |
| Give Him Control (§1.9) | done | Backup/restore, forced settings, daily randomisation of the 41 options, toggle sounds, activity x6, creeping sighting. The red/glitched themed UI is approximated by a red title. |
| Advancements (§2.22) | partial | Script journal + action-bar toast when the `advancements` option is on. Found-footage developer easter eggs skipped. |
| Custom sighting API (§2.23) | done | `/scriptevent ftf:summon {json}`. |
| Nametag, emissive_layers, from_the_pants (§2.24) | done | `hoebrine_returns` falls back to the default skin (no hoebrine model). |
| vanilla_sightings (mannequin) | not ported | Not needed: the custom entity already uses player geometry. |
| Lights Out integration (A7) | not ported | Dropped per spec. |
| Languages | partial | en_US only (sign/caption/donation texts are resolved in script, local + Swedish). Other 31 Java lang files not converted. |
| Armor-stand silent break sounds | not needed | No armor stands are used. |
