# FiguraBridge tools

Plan + status: `docs/PORT_FIGURA.md`. Shared model code: `tools/ModelCore/` (`bbmodel.py` reads Figura's .bbmodel files).

## avatar_pack.py - F1-lite test pack (no Lua yet)
Turns a Figura avatar folder into a Bedrock resource pack that replaces YOUR player model (single-player test).
Vanilla Bedrock player animations drive the avatar through Figura's parent types (Head/Body/arms/legs/Cape).

    python -I tools/FiguraBridge/avatar_pack.py AVATAR_DIR OUT_RP --vanilla <bedrock-samples>/resource_pack --models main

`--vanilla` = Mojang's bedrock-samples resource_pack (player.entity.json is copied from it). Not included: scripts
(hair physics, emotes, action wheel), extra textures beyond the first, non-body parts (Hud, Skull, Item pivots...).
Those are the native runtime's job (F2-F4).

Test avatars used (MIT, by Gakuto1112, not in this repo): github.com/Gakuto1112/FiguraAvatarTemplate (a57327d),
github.com/Gakuto1112/StrawberryMiku (a710a6b).

## compile.py - full compile (Figura NBT + rig contract + driven Bedrock pack)
    python3 -I tools/FiguraBridge/compile.py AVATAR_DIR OUT_DIR --vanilla <bedrock-samples>/resource_pack [--uuid PLAYER_UUID]
    python3 -I tools/FiguraBridge/test_compile.py          # compiles the 3 test avatars into /home/claude/out/compiled/

- `figura_nbt.py`: line-by-line port of Figura 0.1.6 LocalAvatarLoader + BlockbenchParser2 (+V4/V5) +
  AvatarMetadataParser -> `OUT_DIR/avatar/figura_avatar.json` (byte arrays as `{"__file": ...}` side files,
  scripts as original Lua text, collections "pr" as `{"__bytes": [...]}`).
- `OUT_DIR/rig.json`: parts in pre-order (bone `p<i>`), textureFiles / textureSets, sounds, Molang variable doc.
- `OUT_DIR/rp/`: one bone per Figura part (no bind rotation), `animation.figura.drive` reads `v.fb<i>{r,p,s}{x,y,z}`,
  per-texture-file render controllers `figura_p<f>` / `figura_s<f>` read `v.fb<i>v`, `v.fb<i>t`, `v.fb<i>e`;
  vanilla player controllers render unless `v.fbvan` is 0 (the runtime writes it on the avatar's player only, so other players stay visible); worn armor and elytra are hidden on the avatar's player (`attachables/fb_armor`, `v.fbnoarmor`).

## Stick figure armor (`stick_armor.py`)

Fitted armor for the stick avatar: segmented plates on copies of the stick's own bones (same names, parents, pivots),
drawn by the player entity's own render controllers, so `animation.figura.drive` moves the armor with the limbs.
Per slot (head circlet + fin, chest vest/breastplate/pauldrons/bracers, belt/thighs/knees, shins/boots), one texture
per material (leather, chainmail, iron, golden, diamond, netherite, copper, turtle) picked with
`query.is_item_name_any('slot.armor.*')`. Only on the avatar's player, only while the stick is the installed avatar.

    python3 -I tools/FiguraBridge/compile.py <Stick Figures avatar> OUT --vanilla <bedrock-samples>/resource_pack --armor stick

`--armor stick` runs it as part of the build (don't patch an installed pack by hand: the next compile drops it).
Standalone (patches an existing pack, idempotent):

    python3 -I tools/FiguraBridge/stick_armor.py RP/models/entity/figura_stick.geo.json RP/entity/player.entity.json OUT

Style (`STYLE`, default `mix`): `mix` = the original light-rail armor and Novak Glass in one suit
(`build_suit_mix`): the original's metal plates stay the structure (enclosed helmet, layered petals, belt, knee caps,
back and thigh-back plates, foot shells - a polished finish, leather neutral and dyed with the item like the glass),
its main faces are crystal panes (visor over a dark backing, breastplate V, spine shard, outer shoulder petals,
forearm blades, thigh plates, greaves, calf guards) and its light rails run over them in the glass's neon hue.
Per slot three draws: metal, rails, then glass (RF's glass refracts what's already drawn). The glass parts follow
the rules below. `glass` = all-glass suit (`build_suit_glass`): the plates bind the game's own vanilla armor textures (64x32, layer 1 for
helmet/chest/boots, layer 2 for leggings); Renderer Foundation's glass armor recognises those textures and draws its
Novak Glass / Cloud Sea glass on the stick's plates (leather takes the item's dye through
`entity_alphatest_change_color` + the controller's `color`). RF draws the neon outline, rim light and lens bevel along
the edges of the vanilla texture's painted face areas, so each plate is mapped edge to edge onto one **clean island**
(`ISLAND`: painted in every material, a real edge all round) at about one texel per model px - then its whole
silhouette carries the outline, like a cube face of the regular glass armor. The suit (`build_suit_glass`): a crystal
orb helmet (front unwrap, outline round its silhouette), V breastplate + back plate, pauldrons, bracers, belt
front/back, thigh guards front/back, greaves + calf guards, foot shells; no light rails (RF draws the light; fewer
draws, no timers). `test_stick_glass.py` checks the islands against RF's data (`glass_data.json`, from
`glass_armor_data.hlsli`) and that every plate stays inside one island and reaches its border.
`stick_armor_glass_preview.py` renders the stick at in-game scale over a real screenshot with a numpy port of RF's
glass pixel shader (checked against an in-game shot of the regular glass armor) - judge changes there.
`STYLE = "metal"` = the earlier painted metal atlas with light rails. Not on the stick: the enchanted edge flow (an
entity controller can't see the item's enchantment); turtle helmet: RF has no turtle variant, it stays textured.

## Servers (many players)

One `player.entity.json` draws every player in the world, but only the avatar's own player gets the runtime's
variables. Everything avatar-specific is gated on `(v.fbvan ?? 2) < 2` (true only there): the drive / item animations
and, as the first term, every avatar render controller condition, so other players cost one variable lookup each.
The runtime logs `[figurabridge] perf 10s: ...` to `config/figurabridge.log` (FiguraBridge's own time per frame vs
the game's frame time; tuning `perfLog` 0 turns it off).
