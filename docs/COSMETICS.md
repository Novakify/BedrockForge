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

## Status
- 2026-11-17: pipeline + Novak Karambit built; in-game check pending (see NEXT_SESSION.md).
