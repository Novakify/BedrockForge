# Port plan: Cinematic Interactions (Java) -> Bedrock Forge

## STATUS (2026-10-29, RF branch `cinematic`) - how it was actually built
Not the add-on route below: everything is in RF (one mod with Spatial GUI). `src/Cinematic.cpp` (scene, camera,
picking, slot detection, rendering), `src/CinematicGame.cpp` (LL queries), `src/SpatialPanel.cpp` (screen routing,
UI redirect, GameInput remap), `src/SpatialCamera.cpp` (CameraComponent write before LevelRendererCamera::preRenderUpdate).
- Hidden vanilla screen: the UI pass is redirected into our target; slots are found by reading it back (dark bevel
  lines of the slot squares); item icons on the block are crops of that image (keyed background), block items are
  unwrapped onto cubes from their isometric icon; enchant glint / mean colour are read from the crops.
- Clicks: the hidden screen's cursor is parked in an empty corner and moved onto the target slot for a press
  (hover 45 ms, then down); own clicks (a move's beat, a take) are injected the same way.
- Stations with scenes (Java timelines ported 1:1): crafting table, furnace, smoker, blast furnace, anvil (hammer,
  3 blows), grindstone, stonecutter (dial of shapes from the recipe buttons), smithing, cartography.
  To do: loom (board of pattern buttons, cloth), enchanting (runes from the 3 option buttons) - need screen dumps
  (`dumpUi` + `devScenes` in config/CinematicInteractions.json).
- Not portable 1:1: real 3D item models (icons/cubes instead), map pictures (map item icon), banner layers, sounds,
  the Left Alt orbit, mouse-wheel scrolling of the dial/board, other players' views.


Step 0 research (2026-10-27). Source + docs read, no code written. Do Spatial GUI M0-M1 first (shared layer).

## Source + licence
- Repo: github.com/dcvke/CinematicInteractions by Southware. Code **MIT**. Port allowed: keep MIT notice + credit.
- Bundled "Just a Hammer" model (RDiamonds, **CC BY 4.0**): may be reused with credit + note of changes
  (see their CREDITS.md). Easiest: make our own hammer, or credit exactly as they do.
- Reference code: NeoForge 1.21.1 in `src/main/java/dev/dyslexic/cinematic/` (173 files, ~45k lines);
  Fabric 1.21.11-26.3 copy in `multiversion/`. Stonecutter build. Dev guide: `docs/wiki/Development.md`.
- Core classes read: `client/Scene` (1086, approach -> work -> exit), `client/CameraRig` (185),
  `client/TableStation` (1079), `client/Hands` (614) + `ArmMesh`/`LegMesh`, `client/Picking` (127),
  `client/ClientMenus` (114), `station/MenuActions` (250), per-station classes (Anvil 839, Furnace 670, ...).

## The key finding: "client-only mode"
The mod has two modes:
- **Full mode (mod on server):** custom packets, items physically stay on the crafting table (`table/TableStore`),
  observers see poses. Needs server code.
- **Client-only mode (server without the mod):** the client sends a normal block use, the server opens its **normal
  menu**, the client draws the scene on top, and **every item move is a standard menu click**. Camera stays within
  0.35 blocks of the eyes and never goes through walls. Supported: crafting table, furnace, smoker, blast furnace,
  enchanting, anvil, grindstone, smithing, stonecutter, loom, cartography.

Client-only mode is exactly what Bedrock needs: keep the vanilla container screen open but hidden, draw our 3D
scene, translate clicks into that screen's slot actions. No server logic, item moves stay vanilla-valid.
This is the same "drive the hidden vanilla screen" layer as Spatial GUI -> build it once there.

## How a scene works (Java)
1. **Approach:** right-click a station -> camera glides from the eyes to the station's lean pose (`CameraRig`:
   lerp with short-way yaw, FOV follows the lean, clamped to what the head can reach).
2. **Work:** station supplies layout (where slots sit on the block), camera pose, controls, timeline. Hover/pick by
   ray (`Picking`), skin-textured arms (`ArmMesh`, `Hands`) animate reaches, items render on the block surfaces.
   Camera breathes + drifts toward the cursor; "dramatic moment" tremor (anvil strike).
3. **Exit:** Esc / inventory / movement -> camera glides back to wherever the eyes are now.
Extras: Left Alt orbit view, move to a nearby station, recipe guide on the crafting table, settings screen.

## Feature table
Routes: a = add-on only, b = BF client hooks, c = RF D3D12 overlay, d = ShaderForge. SG = reuse Spatial GUI layer.

| Feature | Java | Bedrock route | Difficulty |
|---|---|---|---|
| Detect station use + which block | block-use event | a: script `playerInteractWithBlock` (before/after) **or** b: LL event | Easy |
| Camera approach / exit glide | Camera mixin + CameraRig | a: `/camera @s set minecraft:free ease <t> in_out_sine pos .. rot ..` then `clear`; b: camera hook if smoother needed | Easy |
| Camera breathing, cursor drift, tremor | CameraRig per frame | b: per-frame camera offset hook (FOV hook exists; add pos/rot) - script camera is too coarse | Medium |
| FOV follows lean | GameRenderer | b: `BfFovHook` | Easy |
| Hide normal menu, keep it as input target | client-only mode | SG layer (JSON UI alpha 0 / capture-cover) | Medium (SG) |
| Clicks -> vanilla slot actions | standard menu clicks | SG layer: remap cursor onto hidden screen's slot positions | Medium (SG) |
| Items drawn on the block (table grid, furnace openings) | item renderer at layout points | c: item icons as billboards in RF 3D pass (needs item icon atlas) **or** a: script-spawned display entities | Medium-hard |
| Skin arms reaching | ArmMesh + Hands | a: RP first-person player animations (bones rightArm/leftArm), triggered by script/molang variable | Medium |
| Hover highlight / pick by ray | Picking | b: ray vs layout boxes in BF; highlight in RF | Medium |
| Recipe guide (crafting table) | TableRecipeGuide | c: ImGui panel; recipe data from BF (recipe registry) | Medium-hard |
| "Lay out" ingredients | queued menu clicks | b: sequence of slot clicks on the hidden screen | Medium |
| Anvil hammer + 3 strikes | HammerModel + timeline | a: attachable/entity model + animation; tremor via b | Medium |
| Furnace progress on block | FurnaceRenderer | c: read container progress (BF) and draw | Medium |
| Lectern / bookshelf reading | OpenBook, BookPages | c: textured book panel | Hard (later) |
| Orbit view (Left Alt) | FreeLook | b: BF freelook module (planned anyway) | Medium |
| Move between stations | Scene transitions | a+b | Medium (later) |
| Observer poses (other players) | packets | out of scope (single-player first) | - |
| Settings | SettingsScreen | c: RF ImGui + BF config | Easy |

## What's realistic
- Very likely: camera glide in/out, FOV lean, arm animations, crafting table / furnace / anvil in client-only style.
- Depends on Spatial GUI M0: hiding + driving the vanilla screen (if capture fails we still can hide via JSON UI and
  remap the cursor, because we draw our own scene here anyway - Cinematic needs less from capture than Spatial GUI).
- Long tail: all 19 stations, books, beacon, observer sync. Do 3 stations, then decide.

## Milestones
- **M1 (after SG M1):** crafting table only: script detects use, `/camera` glide in/out, hidden vanilla crafting
  screen, 3x3 grid hotspots on the table top mapped to slot clicks, result pickup, Esc exits. No arms yet.
- **M2:** arms (RP animations), item icons on the table, hover highlight, furnace + anvil (hammer, tremor).
- **M3:** recipe guide + lay-out, settings in RF ImGui, orbit view.
- **M4:** docs + MIT/CC BY credits, commit, NEXT_SESSION.md.

## Risks
- Bedrock container screens differ from Java (pocket/classic layouts, hidden scrolling); slot positions must come
  from the live screen, not hard-coded.
- `/camera` needs cheats/op in the world (fine in own single-player worlds; note it in the README).
- Item rendering in RF needs an item icon source; Bedrock has the atlas at runtime but no public API - spike in M2.
- Keep the 0.35-block / no-wall-clip rule from client-only mode, or the camera shows through blocks.

## Starter line
"New session. Read docs/NEXT_SESSION.md, docs/PLAN_JAVA_PORTS.md and docs/PORT_CINEMATIC.md. Goal: Cinematic
Interactions M1 (crafting table, client-only style). Requires Spatial GUI M1 done. Stop when done."
