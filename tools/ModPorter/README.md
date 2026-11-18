# ModPorter v0: Java entity mods → Bedrock add-ons

Converts Forge 1.20.1 entity mods (MCreator + GeckoLib especially) into a Bedrock behavior + resource pack.
It reads the jar's **bytecode with `javap`** and never runs the mod.

| Stage | What | Status |
|---|---|---|
| `extract.py` | per entity: registry id, size, attributes (SRG-mapped), AI goals, sounds, procedure→event map, model/texture/animation paths | Working on From The Caves (49/50 auto-mapped) |
| `generate.py` | GeckoLib geo + animations (renamed per file to avoid `geometry.unknown` / `animation.model.*` collisions), textures, client entities with idle/walk/attack controllers, spawn eggs, sounds, names; BP entities with the real stats + a generic hostile AI | Working |
| `validate.py` | JSON, geometry/texture/animation references, animated bones vs geometry, UV vs texture aspect, BP/RP id match | 0 errors on From The Caves |
| `extract_src.py` | SOURCE mode for open-source mods (no jar): same entities.json + an assets.zip for generate.py | Working on Sculk Horde (32/32 mobs) |
| `overlays/<modid>/` | our own Script API code for the mod's mechanics + entity overrides + items/recipes, merged automatically | From The Caves v0; The Dweller (full AI); Sculk Horde v0 (gravemind, infestation, nodes, abilities) |

Java-code models: `HumanoidModel`/`PlayerModel` map to Bedrock's built-in `geometry.humanoid.custom`.
Other hand-written Java models aren't converted yet (they fall back to the mod's first GeckoLib model).

## Run (on your PC, on a jar you own)
```powershell
.\Port-Mod.ps1 -Jar "C:\path\to\mod.jar"
```
Needs Python 3.11+ and a JDK (for `javap`). Output: `C:\BedrockModding\Ports\<mod>\`.

## Licence rule
The tool is ours. **The converted assets aren't.** If the original mod is All Rights Reserved (From The Caves,
The Masked, The Knocker), its port is for private testing only: don't share or post the add-on.
Never commit generated ports to this repo.
