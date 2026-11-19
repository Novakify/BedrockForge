# 3D model cosmetics (Knives, 3D Skins, Garage cars)

Our own designs only: every model, texture and sound is made here (tools/Cosmetics), nothing taken from another client.
Purxiy is only the reference for *what* the feature does.

## Pipeline (decided 2026-11-17)
| Option | Follows hand / bones | FPS | Hive | Verdict |
|---|---|---|---|---|
| RF draws meshes in the atmos pass (like wings/cape) | no: a fullscreen pass only knows the player position, not the arm's swing pose | a pass per frame | yes | wings/capes/auras only |
| Figura / GeckoBridge runtime | only the avatar rig (FiguraBridge drives v.fb* per bone every frame) | CPU per frame | own player | not for held items |
| **Resource-pack attachables + poly_mesh** | **yes, native: item bone, swing, first/third person** | **~0: drawn by the game in its own item pass** | **yes (global pack, client only)** | **chosen** |

Bedrock Forge does not draw anything for these; it only *switches* them. Every HUD frame `src/client/Cosmetics.cpp`
writes **public** Molang variables on the players (the access flag is set on our variables, so no player.entity.json
override is needed - Hive's and FiguraBridge's stay untouched):

| variable | meaning |
|---|---|
| `v.bfknife` | 0 = off (vanilla sword), n = knife design n (Knives module "Knife") |
| `v.bfkdraw` | seconds since you drew a sword (99 = no flip) |
| `v.bfkinsp` | seconds since the inspect key (99 = never) |

Other players get 0 (twice a second), so only you see your knife and the attachables never read a missing variable.
No Bedrock Forge = nothing written = vanilla look.

## Knives
- Models: `tools/Cosmetics/knives.py` (mesh kit `mesh.py`, offline turntable `preview.py`). Trident frame like the
  World FX weapons: grip on (0, 24, 0), item along +y, flat facing +-z; same hold animations as World FX.
- Pack: `python3 -I tools/Cosmetics/build_pack.py <vanilla textures/items>` -> `packs/BFCosmetics` (global pack,
  **below** the atmosphere packs). `add_knives()` is the shared patch for any sword attachable (atmospheres next).
- Each knife has its own 512 atlas (tools/Cosmetics/materials.py) and model; alpha = World FX material id.
- Motion (pack Molang, `PRE` in build_pack.py): draw = one spin round the finger ring (0.6 s); inspect = lift + turn
  the flat to you, two spins, settle (2.3 s). Inspect key is unbound by default (Fernando picks it).
- Designs (Knives module "Knife"): 1 Karambit, 2 Balisong, 3 Dagger.

## Designs, finishes, audit, animation proposal
See docs/COSMETICS_REFERENCE.md (v4: Karambit / Balisong / Dagger, each a real-world design with its own
materials; v1-v3 shared a neon scheme and were dropped on Fernando's review).

## Knives customization (2026-11-18)
Settings (Right Shift > Mods > Cosmetics > Knives, gear or right-click the row):
- **Knife** Karambit / Balisong / Dagger, and **Applies to**: All swords; By sword type (wooden ... netherite each
  Main knife / Karambit / Balisong / Dagger / Vanilla); One hotbar slot (1-9); Renamed swords (anvil name containing
  karambit, balisong/butterfly, dagger, or knife = main knife). Only your client reads the name or slot.
- **Finish** Random (default: a new one on every draw) / Factory / Blackout (black DLC, raw edge) / Gold (TiN) /
  Fade (anodised straw -> violet -> blue). Made from each knife's own atlas (tools/Cosmetics/finishes.py), so the
  Damascus, stonewash and wear stay under the coating.
- **Effect colour** Random (default: a new one every swing) / Steel / Violet / Ember / Gold / Crimson. BF writes the
  colour (v.bfkcr/cg/cb); each particle event hands it to the emitter with pre_effect_script and the particle tints
  white-hot -> colour -> out. If the game doesn't pass it, particles fall back to steel white.
- **Slash style** Matched to the knife (default: karambit crescent, balisong trail, dagger thrust streak) / Trail /
  Crescent / Thrust streak - same swing timing, different light.
- Size, left/right, up/down (first person), third person on/off, enchantment glint on/off, quiet sounds, plus the
  earlier inspect key (I), idle tricks + interval, speed, draw, sounds, slash effects, hit effects.

### Quality + performance audit (offline, 2026-11-18)
- Finishes judged on studio renders (docs/img/cosmetics/finishes.png): fixed a seam where the karambit's fade
  wrapped round the ring (ring + rim now take the handle-end colour), darkened Blackout so the stonewash doesn't
  read grey. Crescent sprite redrawn as a real crescent (outer minus offset inner circle, 4x supersampled) after the
  first 16 px band looked pixelated.
- BF: Molang variables are written only when they change (or a 1 s refresh) instead of 18 x 2 hashed writes every
  frame; timers stop changing once they reach 99 s. Other players: rest values every 0.5 s (on) / 2 s (off).
- Pack: 74 animate entries per sword attachable, every one starts with `v.bfk == n` / `v.bfk > 0`, so other
  players' swords cost ~74 failed compares a frame; clips only exist while their event runs. Particles per event:
  trail 6, crescent 1, streak 9, hit ~10 sparks + 1 cut - all additive, 0.1-0.3 s. Textures: 12 knife atlases
  512x512 (~17 MB VRAM with mips) + fx 64x64; sounds 14 short oggs.
- Open risk (Mojang MCPE-160960): particle locators on attachables may spawn at the entity origin instead of the
  tip. Check in game; fallback is particles without a locator, placed by the swing (or BF-drawn World FX trails).

## 3D Skins (plan, not started)
Outer skin layer as voxels, own player first. A pack geometry with one small cube per outer-layer texel (64x64
skin: all outer-layer positions, ~3k cubes, one draw) UV-mapped 1:1, textured by a runtime texture that Bedrock
Forge fills with your skin (FiguraBridge's DynTex), alpha-tested so empty texels vanish; the flat vanilla outer
layer hidden. Needs the player render controller (shared player.entity.json with FiguraBridge, generated by one
tool). Hive: own player only, client side.

## Garage cars (plan, not started)
A parked show-off car is not held by a bone, so here the RF mesh pass IS the right tool: RF draws an original
low-poly car (tools/Cosmetics mesh kit -> RF mesh file) at a world spot you pick in the lobby, with an idle
animation (suspension breathe, wheel turn, light pulse), depth-tested, nothing sent to the server. Look: our UI -
smoked-glass canopy, pearl-white body panels, neon purple light lines and underglow. Needs a new RF capability
(mesh draw in the world pass); designs as previews first.

## Status
- 2026-11-17: pipeline + Karambit built (main 07ad8a1 installed on the PC, game not restarted yet); v4 realistic
  Karambit / Balisong / Dagger in the pack (newer main); in-game check pending (see NEXT_SESSION.md).
