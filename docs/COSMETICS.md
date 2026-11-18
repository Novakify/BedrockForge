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
- Neon parts are the `bfk_neon` bone, material `entity_emissive_alpha`, texture alpha 0.
- Motion (pack Molang, `PRE` in build_pack.py): draw = one spin round the finger ring (0.6 s); inspect = lift + turn
  the flat to you, two spins, settle (2.3 s). Inspect key is unbound by default (Fernando picks it).
- Designs: 1 Novak Karambit. Next: butterfly (balisong flip), dagger.

## Designs: real reference + our twist (Fernando 2026-11-17: "extremely similar, never 1:1")
| Knife | Reference (proportions from real knives) | Our twist |
|---|---|---|
| Novak Karambit (in the pack) | hawkbill claw blade ~ handle length (both ~3.5"), finger ring ~1" inner, curved handle, spine jimping | neon grind line, smoked-glass scales with frosted chamfers, smoked-violet crystal spine, pearl ring with a glowing inner rim |
| Novak Balisong (preview only) | channel-construction balisong: 4" clip point with swedge, 5" handles, pivot + zen pins, kicker, latch on the bite handle | smoked-glass windows in the handles with a neon line, pearl pivots, accent on the latch, the family grind line |
| Novak Dagger (preview only) | Fairbairn-Sykes 3rd pattern: 7" double-edged spear point with centre ridge, short oval guard, vase ring grip, pommel nut | neon ridge fading before the point, smoked-glass core in the guard, blackened rings with bright crests, faceted pearl crystal nut with a neon collar |
Previews: `docs/img/cosmetics/`. Balisong/Dagger join the pack only after the Karambit is verified in game.

## Quality audit (offline, 2026-11-17) - three passes, honest
Judged on the offline turntable in game-like light (Fancy entity shading: diffuse, no spec) and studio light,
at full size and at hand size (~200 px), on dark / sky / grass backgrounds. NOT a substitute for in-game frames.
1. v1 karambit: blade and handle both light -> no contrast, read as one grey plastic piece; grooves were a
   sawtooth; flat grey "pearl" handle looked like plastic. Fixed: mirror grind paints a horizon reflection (dark
   below the grind line, bright band, floor reflection, bright bevel), handle became smoked glass with frosted
   chamfers + liner seam, ring became the pearl accent, grooves are smooth scallops (34 sections).
2. Proportions checked against real karambits (blade ~ handle, ring ~1"): handle lengthened, blade arc and ring
   shrunk. Balisong v1 read as a spear (blade centred, tip hook artefact), window rim read as a UI outline,
   handle stripes came from a round-handle texture on boxes -> spine-anchored clip point, edge meeting the tip,
   contoured slabs with their own machined/anodised texture, quieter glass rim, the family grind line added.
   Dagger v1 grip was zebra stripes (texture period != ridge period) -> crests painted in step with 15 ridges,
   low contrast; pommel spike flattened into a nut; guard got a visible glass core.
3. Hand-size check: silhouettes read at ~200 px (claw + ring, two-slab handle + pins, cross + vase grip); the
   neon stays a thin line, not a glow smear.
Known risks for the in-game pass: poly_mesh normals/winding in the item pass, `entity_emissive_alpha` with
alpha 0 (neon), the first-person scale (1.25 x the sword hold), Molang `c.owning_entity->` with our public flag.

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
- 2026-11-17: pipeline + Novak Karambit built (main 07ad8a1 installed on the PC, game not restarted yet);
  Balisong + Dagger designed (previews); in-game check pending (see NEXT_SESSION.md).
