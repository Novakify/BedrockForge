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
