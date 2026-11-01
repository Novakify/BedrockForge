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
