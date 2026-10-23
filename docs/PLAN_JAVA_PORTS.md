# Plan: Java -> Bedrock ports of "Cinematic Interactions" and "Spatial GUI" (after polish week)

Order: 1) finish/polish (shadows, motion blur check, cleanup)  2) Spatial GUI  3) Cinematic Interactions.
One mod per session. Every session starts by reading docs/NEXT_SESSION.md and this file.

## Step 0 for each mod (first session, research only, cheap)
- Find the mod page + source; record licence (ARR -> private port like From The Caves, never committed).
- List every feature in a table: feature | how Java does it | Bedrock route | difficulty.
- Bedrock routes we have, cheapest first:
  a. add-on only (resource pack JSON UI, script API, /camera presets + easing, animations)
  b. BF client hooks (LeviLamina: UI render events, input, camera/FOV hooks, player state)
  c. RF D3D12 overlay / full-screen pass (draw anything on screen, ImGui, post effects)
  d. RenderDragon materials via ShaderForge (custom shaders)
- Output: docs/PORT_<MOD>.md with the table + milestone list. No code that session.

## Expected shape (to confirm in step 0)
- Spatial GUI: menus/inventory shown in 3D space. Likely needs (b)+(c): read the open screen/inventory from the
  client, render it ourselves as a 3D-projected panel (RF overlay with camera matrices from BF), route input back.
  Bedrock's own JSON UI is 2D only, which is why a pure resource pack falls short.
- Cinematic Interactions: camera moves/animations on interactions. Likely (a)+(b): BF detects the interaction,
  drives the camera (script /camera ease or a camera hook), plus player animations in the RP.

## Milestones per mod
M1 prototype of the single most visible feature -> M2 remaining features -> M3 polish/config menu -> M4 docs.
Commit + push after each milestone; update NEXT_SESSION.md before stopping.
