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

## Research notes (2026-10-23, pages only, nothing downloaded)
- Spatial GUI: MIT licence, source github.com/tastytrash/Spatial-GUI, client-only (Fabric/Forge/NeoForge, needs YACL).
  Shows inventory/container screens as a 3D panel (1st/3rd person, curved screen, blur, open animation, FOV override).
  Port is allowed (keep MIT notice + credit). Bedrock route: BF reads the open container screen + camera, RF draws a
  3D panel (D3D12 overlay with BF camera matrices), input routed back. Realistic: medium-hard; full parity uncertain
  because Bedrock menus are JSON UI - we must either re-draw them ourselves or capture them to a texture.
- Cinematic Interactions (curseforge.com/minecraft/mc-mods/cinematic-interactions, by Southware): MIT code, source
  github.com/dcvke/CinematicInteractions; bundled "Just a Hammer" model by RDiamonds is CC BY 4.0 (credit it).
  Client+server mod. Workstations become animated 1st-person scenes with skin arms: crafting table 3x3 workspace
  (recipe guide, missing-ingredient highlight, auto lay-out), furnace/smoker/blast, crafter, anvil, grindstone,
  smithing, stonecutter, enchanting, brewing, loom, cartography, lectern, chiseled bookshelf, jukebox, campfires,
  beacon. Smooth camera between stations, inventory panel, Left Alt = 3rd person, settings (lean, zoom, bars, shake).
  Bedrock route: camera = /camera ease or BF camera hook (easy); first-person arm animations = RP player animations
  (medium); the in-world interactive workspace replacing each vanilla screen = biggest job (same problem as Spatial
  GUI: draw our own UI + route clicks, and server-side item moves via script API / BF). Do Spatial GUI first: its
  3D-panel + input layer is reused here. Realistic: camera + animations very likely; all 19 stations at full
  parity = long project, do 2-3 stations first (crafting table, furnace, anvil).
- Cinematic Interactions source check (2026-10-27, web pages only): Java/Gradle; reference impl NeoForge 1.21.1 in
  src/main/java/dev/dyslexic/cinematic/, Fabric ports in fabric/ (1.21-1.21.11, 26.2, 26.3). Packages: client scene
  (approach -> active work -> exit), station sessions (each station = layout + camera pose + controls + timeline),
  "vanilla menu actions" (it drives the normal container menus underneath = item moves stay vanilla/server-valid),
  network packets, mixins. Credits: CREDITS.md (Just a Hammer, RDiamonds, CC BY 4.0). Small repo (6 commits).
  Start of the port session: read docs/wiki/Development.md, then the client scene + one station (crafting table).
  Key Bedrock idea to test first: keep the vanilla Bedrock container screen open but hidden, and drive it from
  our 3D workspace (same trick the Java mod uses with vanilla menu actions) -> no custom server logic needed.

## HOW TO START NEXT SESSION (copy-paste)
"New session. Read docs/NEXT_SESSION.md and docs/PLAN_JAVA_PORTS.md. Goal: <one item>. Stop when done."
Polish list first (shadows, motion blur check, cleanup), then Spatial GUI research -> M1, then Cinematic Interactions.
