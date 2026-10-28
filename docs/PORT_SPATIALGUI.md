# Port plan: Spatial GUI (Java) -> Bedrock Forge

Step 0 research (2026-10-27). Source read, no code written. Next: M0 spike (below).

## Source + licence
- Repo: github.com/tastytrash/Spatial-GUI, v1.9.1, **MIT** (copyright 2026 tastytrash). Port allowed: keep the MIT
  notice + credit in the port's README. Code may be read and adapted (we write our own C++, not a copy).
- Client-only. Fabric / Forge / NeoForge via Stonecutter (1.20.1 -> 26.3). Needs YACL (config UI) + Mod Menu.
- 72 Java files. The core is small: `render/` (TextureTargetManager 106, ScreenExtractor 224, SpatialGUIRenderer 594,
  InventoryRenderer 761, WorldBlurRenderer 51) + `util/` (MouseHandlerUtil 292, CameraUtil 129, RenderUtil, MathUtil).
  The other ~40 files are mixins (HUD hiding, camera, hand/avatar pose, input) and mod-compat shims.

## How the Java mod works (the 4 core tricks)
1. **Render the open screen into an offscreen texture.** `TextureTargetManager` makes an RGBA8 + D16 target sized
   window x `renderScalePercent`; `ScreenExtractor` draws the vanilla screen into it with an isolated GuiRenderer
   (so the HUD isn't included). The screen code itself is untouched, so every vanilla/modded screen works.
2. **Draw that texture on a 3D panel in the world.** `QuadBasis(center, right, up, normal, halfW, halfH)` for a flat
   panel, `CylinderBasis(axis, radius, arc, halfH)` for the curved screen. Placed relative to the camera (1st person)
   or the player (3rd person), with drop-shadow layers, opacity, world-light tint, open/close scale + fade animation.
3. **Ray-pick the mouse back onto the screen.** `MouseHandlerUtil.mapMousePosition` -> `RenderUtil.get{Quad,Cylinder}
   ScreenPosition`: cursor/crosshair ray vs panel -> panel UV -> screen pixel; that pixel is what the vanilla screen
   receives as the mouse position. Clicks, drags and hover then "just work" in vanilla code.
4. **Camera + presentation.** FOV override with lerp, 3rd-person camera glide (`transitionDurationMs`), avatar body/head
   rotation, optional world blur behind the panel, HUD/hotbar/hands hidden while open.

## Feature table
Routes (cheapest first): a = add-on only, b = BF client hooks, c = RF D3D12 overlay/full-screen pass, d = ShaderForge.

| Feature | Java does it with | Bedrock route | Difficulty |
|---|---|---|---|
| Know a container/inventory screen is open | Screen mixins | b: LL `AfterUIRenderEvent` already gives the ScreenView; add a screen-name filter (`isHudScreen` exists) | Easy |
| Capture vanilla screen to a texture | Isolated GuiRenderer -> TextureTarget | **c + b: unknown, M0 spike.** Bedrock JSON UI draws straight into the frame (RenderDragon), no public offscreen path | **Hard / risky** |
| Fallback: draw our own inventory panel | (n/a) | c: ImGui/D3D12 panel from BF-read container slots + item icon atlas | Medium-hard, loses vanilla look |
| 3D panel (flat) | QuadBasis mesh | c: textured quad in RF with BF camera matrices (pos, yaw/pitch, FOV already exported for post-FX) | Medium |
| Curved panel | CylinderBasis mesh | c: tessellated cylinder strip, same shader | Easy after flat |
| Mouse/crosshair -> screen coords | Ray vs quad/cylinder | b: ray math in BF; rewrite cursor position the vanilla screen sees (BF has Win32Input) | Medium |
| Hide vanilla 2D screen while panel shows | Not drawing it to the main target | a or c: RP JSON UI override (alpha 0) **or** capture-then-cover | Medium (ties to capture method) |
| HUD/hotbar hidden | Gui mixins | b: BF hud module already toggles HUD elements | Easy |
| Hands hidden in 1st person | HandRenderer mixin | b: hook first-person item render, or a: RP render controller | Easy-medium |
| FOV override + lerp | GameRenderer mixin | b: `BfFovHook` exists | Easy |
| 3rd-person camera glide | Camera mixin | a: `/camera` free + ease (script, single-player) **or** b: camera hook | Easy-medium |
| Avatar faces the panel | Avatar/PlayerModel mixins | a: player animation in RP driven by a script property | Medium |
| World blur behind panel | Blur post pass | c: RF already has blur PSOs (bloom tiles) - reuse | Easy |
| Drop shadow, opacity, light tint | Extra quads + uniforms | c | Easy |
| Open/close animation, easing types | AnimationUtil | c | Easy |
| Config + visual editor | YACL + EditorScreen gizmo | c: RF ImGui window (F6) + BF config file; gizmo editor later | Medium |
| Per-screen enable lists / profiles | profiles.json5 | b: same idea, keyed by Bedrock screen names | Easy |
| Mod compat shims (JEI, Create...) | mixins | not needed on Bedrock | - |

## The one hard question (decides everything)
Bedrock has no "draw this screen into my texture" API. Candidate capture methods for the **M0 spike**, in order:
1. **Two-copy diff at Present (RF only, no engine RE).** Copy the backbuffer right before the UI pass and again at
   Present; the UI is the difference. Needs a reliable "UI pass begins" marker in the D3D12 stream (first draw with a
   UI material PSO). Since BF serves material bins, RF can learn the UI materials' shader hashes and tag their PSOs at
   `CreateGraphicsPipelineState`. Gives full-res vanilla UI incl. text; alpha has to be reconstructed (UI over a known
   background) - test with a black-cleared copy.
2. **Redirect the UI render target (RF).** When the first UI-PSO draw is seen, swap `OMSetRenderTargets` to our own
   RT, restore at Present, then composite. Cleaner alpha, but riskier (descriptor/state assumptions in RenderDragon).
3. **Engine-level: render the ScreenView into an offscreen mce framebuffer (BF).** Most "correct", needs reverse
   engineering of 1.26 UI rendering - only if 1 and 2 fail.
4. **Fallback: our own panel** (draw slots + item icons ourselves, route clicks through the vanilla screen kept open
   but invisible). Always works, but every screen type is extra work and looks less vanilla.

Input works the same for all four: keep the vanilla screen open as the input target and feed it remapped cursor
coordinates from the ray-pick (exactly what the Java mod does).

## M0 status (2026-10-28): code done, NOT yet run in game
- Branch `spatialgui-m0` (RF, from `renderer-foundation` 5246159), commit cb579bb: `src/UiCapture.{h,cpp}` + wiring.
  Pushed to its own branch so `rf-builds` (the installed RF) is untouched; CI builds it as an Actions artifact only.
- Inert unless `"uiCaptureSpike": true` in `mods\RendererFoundation\config\RendererFoundation.json` (then 7 extra D3D12
  hooks: CreateGraphicsPipelineState, CreatePipelineState (counted only), CreateRenderTargetView, SetPipelineState,
  OMSetRenderTargets, DrawInstanced, DrawIndexedInstanced; all pass straight through unless a capture is running).
- UI shader list: `mods\RendererFoundation\config\ui_shaders.txt` is ALREADY on the PC (54 hashes: 36 JSON UI, 18 OreUI,
  made by `tools/gen_ui_shaders.py` from the 1.26.51.01 materials).
- How it works: F7 (or F6 -> "Spatial GUI spike (M0)" -> Capture) -> 1 frame learns RTVs/bindings -> next frames (max 30):
  before the first draw with a UI pipeline the bound RT is copied (A); at Present the back buffer is copied (B, before
  RF's post-FX/ImGui). Output `mods\RendererFoundation\logs\uicapture\<time>\`: a.bmp, b.bmp, diff.bmp, report.txt,
  seen_ps_hashes.txt. The report ends with a VERDICT line.
- Test (needs the game): install the CI artifact DLL as RendererFoundation.dll (rename the current one to
  RendererFoundation.5246159.dll first), set uiCaptureSpike true, launch, enter a world, press E (inventory), press F7,
  wait 1 s, close. Repeat once with a chest. Read report.txt.
- Reading the verdict: "METHOD 1 VIABLE" -> M1 can use diff.bmp-style capture. "INTERMEDIATE target" -> UI draws to an
  offscreen RT first (then method 2 on that RT is actually easier: copy that RT at Present). "NO UI PIPELINES TAGGED" ->
  compare seen_ps_hashes.txt with ui_shaders.txt (bgfx may alter the DXIL) or the PSOs come from pipeline streams.
- Known spike limits: one attempt per capture; assumes the UI target is in RENDER_TARGET state at the draw; MSAA targets
  are reported but not copied; images are .bmp (no PNG encoder needed).

## M0 result (2026-10-28 evening, run in game on the PC)
- Build: branch `spatialgui-m0` a0cd009 (cb579bb + CI step publishing the DLL to branch `spatialgui-m0-builds`, because
  Actions artifact downloads (Azure blob) are blocked from both the cloud session and the PC). Installed for the test only;
  stable RF 5246159 restored afterwards, spike kept as `RendererFoundation.sgm0-a0cd009.dll`.
- Two captures (in world, and with the pause menu open; the inventory key on this profile is not E so it wasn't opened):
  `PSOs created 1377, tagged UI 68` (ui_shaders.txt matches the game's DXIL - tagging WORKS), `UI draws seen 840`,
  `RTVs tracked 70`, but `render target unknown for 840`, `Distinct UI render targets: 0`, no snapshot A.
  VERDICT line: "UI draws seen but their render target is unknown (RTVs copied, not created?)".
- Meaning: the CPU descriptor handles passed to OMSetRenderTargets are not the ones seen in CreateRenderTargetView.
  Likely bgfx copies RTV descriptors (CopyDescriptors/CopyDescriptorsSimple) or binds handles from a heap created before
  RF's hooks were installed.
- NEXT (M0.5, code only, then rerun): hook CreateDescriptorHeap (record heap CPU start + increment + count), CopyDescriptors
  and CopyDescriptorsSimple (propagate handle -> resource), and in OMSetRenderTargets fall back to "which heap/slot is this
  handle" to find the resource. Also log the first unknown handle + its heap in the report. Then rerun inventory + chest.

## M0.5 result (2026-10-28 ~17:40 local, cloud session + PC computer use) - VERDICT: METHOD 1 VIABLE
- RF `spatialgui-m0` b4d9037 (M0.5: CreateDescriptorHeap / CopyDescriptors / CopyDescriptorsSimple / ResourceBarrier
  hooks + unknown-handle report) -> 5401c4b (M0.5b) -> 2d5ac7a (M0.5c). Reports: RF `logs\uicapture\20261028-172009`,
  `-173145`, `-173940` (+ `_previews\a/b/diff.png`).
- Findings: 0 RTV descriptor copies. The 3 unknown UI handles sat one slot apart, rotated per frame, and were in no heap
  created after RF hooked = the **3 swap-chain back-buffer RTVs, created before the hooks**. M0.5b learns them at
  Present (a tracking frame whose UI draws used exactly one unknown handle -> that frame's back buffer): 3 learned,
  0 unknown, every UI draw resolved, target = 3840x2054 R8G8B8A8_UNORM back buffer.
- Snapshot A "faulted" in M0.5/M0.5b because snapshot() called the now-hooked ResourceBarrier -> re-locked gMutex ->
  std::mutex threw inside __try (own bug, fixed by a thread_local guard in M0.5c). The barrier-guess fallback picked the
  wrong target (2626x1404 R11G11B10 world RT) - kept as report-only.
- M0.5c pause menu: A = world only, B = world + menu, diff = UI layer (+ the menu's full-screen dim) ->
  **"METHOD 1 VIABLE: UI draws straight into the back buffer"**. First UI draw: `jsonui UIBlit/Transparent`, ~26-28 UI
  draws per frame. Game stable on all 3 runs.
- Not captured yet: inventory + chest. Computer use can't open them: inventory is on a mouse side button (-94) and
  injected right clicks never reach the game (CPS counter stays R0). Adding keyboard binds in options.txt
  (`inventory:-94,69` / `use:-98,85`, either order) did NOT work - the game kept them in the file but ignored them.
  Fernando has to add E in Settings > Keyboard & Mouse (or open the chest himself).
- Decision for M1: build on the known back-buffer handles with **method 2 (redirect)** rather than diffing - clean
  alpha, and the flat 2D screen disappears from the back buffer for free.

## M1 v0 (2026-10-28 ~18:00-18:30 local, RF `spatialgui-m0` b55aae5 -> 4d7df40) - TESTED ON THE PC
- `src/SpatialPanel.{h,cpp}` + `shaders/panel.hlsl` (-> `src/PanelShaders.h` via `tools/gen_panel.py`, dxc 1.8.2505.1).
- F8 (`uiPanelKey`) or F6 > "Spatial GUI panel (M1)". Needs `uiCaptureSpike: true` (redirect lives in the UiCapture hooks).
- Redirect: first UI-PSO draw on a back-buffer RTV -> clear + bind our transparent target; later binds of a back-buffer
  RTV in that frame -> our target. Present: world copy zoomed (FOV lerp) + dimmed, UI target drawn as a perspective
  quad (size/tilt/yaw/offset sliders, eased open/close, thin frame), mouse ray-picked through the quad.
- WORKS (verified in game): title screen + in-world pause menu render on the tilted 3D panel over the zoomed/dimmed world,
  F8 toggles cleanly back to normal, no crashes in ~6 launches. **Title screen input works through the panel**: hover
  follows the panel position and clicking "Settings" at its panel position opened Settings (WM_* coordinate remap).
- OPEN 1 - in-world input: the pause menu in a world ignores the remap - its hover follows the REAL cursor.
  v0.2/v0.3 diag (`logs\uicapture\panel_diag.txt`, written on F8 off): GetCursorPos ~1 call/frame (= RF's own ImGui
  poll, not the game), only 3 WM_MOUSEMOVE for 3 moves (all remapped), **0 mouse WM_INPUT**. The exe imports
  `GameInputCreate` -> in-world menus read the mouse through **GameInput**. NEXT: hook GameInputCreate ->
  IGameInput::GetCurrentReading / IGameInputReading::GetMouseState and remap there (positionX/Y are accumulated
  deltas, so remap = offset between real and ray-picked position), or find the screen-space cursor the game keeps
  and patch it via BF.
- OPEN 2 - Settings (OreUI/Gameface) renders flat, not on the panel (different path) - out of M1 scope (inventory and
  containers are JSON UI).
- OPEN 3 - colours on the panel look slightly washed (UI blended onto transparent black; alpha mode guess). Tune
  `Alpha boost` / try sqrt alpha once the inventory is visible.
- Known v0 limits: HUD goes onto the panel too (needs BF screen detection: export the current screen name so the panel
  only opens for inventory/containers); bloom/post FX skipped while the panel is shown.
- Inventory/chest still not opened by computer use (see M0.5 result) - Fernando should bind E (Settings > Keyboard &
  Mouse) so a session can test the real target screens.

## Milestones
- **M0 spike (1 session, PC needed):** RF logs PSO creation for UI materials; try method 1 on the inventory screen;
  dump one captured frame to a PNG in RF's log folder. Exit: "capture works / doesn't" + which method.
- **M1 prototype:** inventory shown as a flat 3D panel in 1st person, mouse works via ray-pick, HUD hidden, FOV lerp.
- **M2:** containers (chest, furnace, crafting), curved panel, 3rd-person mode with camera glide + avatar facing,
  world blur, drop shadow, open/close animation.
- **M3:** RF ImGui settings (all the Java options that make sense), per-screen lists, crash/disable guard.
- **M4:** docs + MIT credit, commit, update NEXT_SESSION.md.
Reused later by Cinematic Interactions: the panel renderer, ray-pick and "drive the hidden vanilla screen" layer.

## Risks
- Capture (above) is the whole project; if only the fallback works, scope per screen grows a lot.
- RF draws at Present = after Bedrock's UI. Panel must also hide the original 2D screen, or both show.
- Bedrock screens are JSON UI with their own scaling; cursor remap must use the screen's real size (`view.mSize`).
- Controllers/touch: out of scope (keyboard + mouse, Windows only).
- Single-player / own worlds only, like the rest of Bedrock Forge.

## Starter line
"New session. Read docs/NEXT_SESSION.md, docs/PLAN_JAVA_PORTS.md and docs/PORT_SPATIALGUI.md. Goal: Spatial GUI M0
capture spike (RF UI-PSO tagging + method 1 on the inventory screen). Stop when done."
