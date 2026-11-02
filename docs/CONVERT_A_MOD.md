# Converting a Java mod to Bedrock: the standard pipeline (read first for ANY new mod)

ONE command for everything Java: mod jars, shaderpacks, resource packs, or a folder with several of them.
JavaPort routes each part to our own converter and writes ready dev packs + a report of what's left.
Private use only: credit the authors, ask Fernando about licences.

    python3 -I tools/JavaPort/port.py <mod.jar | shaderpack.zip | folder> <outdir> [--name "Pack Name"] [--install <com.mojang>]
    # -> <outdir>/packs/<Name> BP|RP, PORT_REPORT.md, port.json (packs, required DLLs, shader + native work)
    # optional per-mod overrides: tools/GeckoBridge/mods/<modid>.json

| Part of the Java input | Our converter |
|---|---|
| feasibility (APIs, mixins, rendering refs) | JavaModAnalyzer |
| mobs, armour, items, animations, textures | GeckoBridge + ModelCore |
| blocks, items, recipes, loot, sounds, particles, dimensions, structures, fog | ModPorter |
| the mod's Java code | JavaBridge |
| mob AI | MobAI (GeckoBridge goals + JavaBridge GoalRunner) |
| shaderpacks / core shaders | ShaderForge + VVPackGen (worksheet generated per pack; GLSL->HLSL by hand) |
| custom GL rendering, screens, client mixins | Bedrock Forge (src/) + Renderer Foundation (RF line) - listed in the report |
| Figura avatars (avatar.json + bbmodel/Lua) | FiguraBridge plug-in (tools/FiguraBridge/javaport_plugin.py): RP + figura/<slug> avatar data for the Bedrock Forge runtime |

**New converters plug in without touching JavaPort:** add `tools/<Name>/javaport_plugin.py` with `KIND`,
`detect(path, names) -> bool` and `port(path, out, name, rep)` (contract in the header of tools/JavaPort/port.py).
FiguraBridge should ship one so `port.py` routes Figura avatars to it automatically.

## What you get (all automatic)
| Layer | Tool | What it does |
|---|---|---|
| Models + animations | GeckoBridge (`tools/GeckoBridge`) | GeckoLib 4/5 + AzureLib geo/animations (easings baked), controllers, triggers, glow, armour, held items, animated textures |
| Non-GeckoLib mobs | `vanilla.py` | Java model bytecode -> geometry, setupAnim -> Molang |
| Content | ModPorter (`tools/ModPorter`) | blocks, items, recipes, loot, sounds, particles, dimensions, structures, fog, paintings |
| The mod's own code | JavaBridge (`tools/JavaBridge`) | bytecode -> Script API JS: procedures, entity/block/item/effect hooks, events, commands |
| **Mob AI (MobAI)** | `build.py goals_of` + JavaBridge GoalRunner | see below |

## MobAI (docs/PORT_MOBAI.md) - mobs act like they do in Java
- Every goal from `registerGoals` with its Java priority and values (speeds, ranges, reach). Target/avoid goals hit the
  **exact** mob types (each converted mob gets the family `<ns>_<id>`), not "the player".
- Anonymous MCreator goals are parsed; goals with their own `canUse/canContinueToUse` are **gated**: the translated
  Java condition switches the Bedrock behaviour on/off (`gb:g<i>_on/_off`). Goals written from scratch run their
  translated `start/tick/stop` in the GoalRunner.
- Mobs with **no** goals in Java get **no** Bedrock AI (they do only what the mod's code says).
- Procedures: `setTarget` (tag `gbt_<family>`), `getTarget` (forced -> attacker -> nearest player in FOLLOW_RANGE
  while the Java target goal is active), `lookAt` (rotation only), `navigation.moveTo` (Bedrock pathfinder via an
  invisible `gb_nav_point`), MoveControl steering, `doHurtTarget`.
- Report: `report.json` -> `mobai` (mapped / gated / custom / unmapped goals). **Unmapped must be 0**; add new vanilla
  goals to `GOALS` in build.py when a mod uses one we haven't met.

## Checks before installing (every mod)
1. `python3 -I tools/GeckoBridge/validate.py "<out>/<Name> RP" "<out>/<Name> BP"` -> 0 errors.
2. Smoke test the scripts: copy `<out>/<Name> BP/scripts/javabridge` to `tools/JavaBridge/test/jb`, run
   `node smoke.mjs` there -> 0 errors (ticks one of every mob 200 ticks on a fake Script API).
3. In game: ContentLog (logs folder of the LeviLauncher instance) has no errors for the pack. A script error at load
   kills the WHOLE script graph (gb_runtime -> worldgen -> JavaBridge): treat any `[Scripting][error]` as a blocker.
4. Optional parity check vs Java: `tools/MobAITrace` (Java recorder mod) + `/tag @s add gb_trace` on Bedrock.

## Inputs that are not in git (rebuild them in a new cloud session)
- `~/mappings/srg_1.20.1.json`: `tools/ModelCore/make_srg.py <client mappings.txt> <MCPConfig joined.tsrg> <out>`.
  Both inputs are on Fernando's PC: `curseforge\minecraft\Install\libraries\net\minecraft\client\1.20.1-*\` and
  `...\de\oceanlabs\mcp\mcp_config\1.20.1-*\*.zip` (config/joined.tsrg).
- `/home/claude/tools/mcdata` (`npm pack minecraft-data`), `/home/claude/tools/geyser/sounds.json` (GeyserMC/mappings),
  `/home/claude/tools/packconv/textures.json` (GeyserMC/PackConverter). `tools/vanilla/*` (vanilla retexture lists)
  is optional.

## Installing
Dev packs live in `<instance>\Minecraft Bedrock\Users\Shared\games\com.mojang\development_*_packs`
(`<instance>` = `%APPDATA%\levilauncher.exe\versions\1.26.51.01`). Copy changed files only, keep the installed
`scripts/javabridge/config.js` "structures" map if your build has no structures, Save & Quit, reopen the world.
New entity types need a world reopen; script-only changes work with `/reload`.

## Performance check in game
`/scriptevent gb:perf` (run it again to stop) prints every 5 s: server TPS (20 = full speed) and the average/worst
ms per tick of each converted-pack job (mobTick, goals, steer, playerTick, effects, serverTick, worldgen, anim).
Mob logic and goals are time-budgeted round-robin and mobs over 64 blocks from every player run at quarter rate,
so a crowd slows mob thinking instead of the game. Also check the content log: a client-side Molang error repeats
every frame, and thousands of log writes a minute stutter the game on their own.
