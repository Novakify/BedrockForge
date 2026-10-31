# Bedrock Forge glass client menu (2026-10-31)

Fernando's brief: a full client-style menu instead of the small box, heavily inspired by the Blueberry client's layout
(glass that warps the world, an opening screen with four big buttons), in our own **neon purple + white** palette, with
LyricsFlow in it, and the same glass + glow on the LyricsFlow world panel.

Prototype (look + shader reference, WebGL): claude.ai artifact "Bedrock Forge Glass UI".

## What it is
- **Right Shift** (or Insert) opens a **glass wheel**: MODS (top), HUD (left), SHADERS (right), SETTINGS (bottom).
  The world blurs, the middle stays sharp, the petals bend the world like lenses. Click outside / Esc closes.
- **Window**: icon rail (Mods, Client, Shaders, LyricsFlow; pencil = HUD tab), back button, tab pills
  (Quick Access = pinned modules, Visual, HUD, Combat, Camera, Misc), search (just type), 2-column module grid.
  Left click toggles, gear / right click = module page, **middle click pins to Quick Access**, wheel scrolls.
- **Pages**: module settings (real Setting toggles / sliders, drag sliders), Shaderpacks (real switch via
  `shaderpack.txt`, applies after restart), Client (blur / refraction / frost + Off/Low/Balanced/Max presets, wheel on
  open, blur the world), LyricsFlow (module on/off, Show lyrics = O, Open the LyricsFlow panel, keys).
- Settings live in the hidden module `client_ui` (modules.json).

## How it draws
- BF (`src/client/Menu.cpp`) still pushes an empty native dialog for mouse + focus and cancels its rendering, then
  builds screen-space glyph quads every frame and hands them to `FrameStore::setOverlay` (LyricsFlow's frame store,
  `src/lyrics/core/Engine.*`). The overlay rides on top of the lyric glyphs and keeps the frame "fresh" even with the
  lyricsflow module off.
- Text = Outfit / Outfit Bold MSDF (always kept in an atlas slot), icons = `resources/client/icons.png` (16 white line
  icons, `icons.json` rects). Design units: the screen is 760 tall (the prototype's 1280 x 760), converted per frame.
- RF (`shaders/glyph.hlsl`, `src/LyricsPass.cpp`, branch clientui/rf): when a frame has GLASS / BACKDROP glyphs, the back
  buffer is copied, downsampled to 1/4 and blurred (2 gaussian rounds), and glass shapes sample it with lens refraction,
  chromatic split, rim light and a violet inner glow. New flags in `BfLyrics.h` (identical in both repos):
  `BF_GLYPH_GLASS`, `BF_GLYPH_PETAL` (ring sector), `BF_GLYPH_BACKDROP` (full-screen frost with a clear circle).
- Soft SOLID edges (glowW > 1.5) now fade to 0 at the quad edge (glows / shadows had hard rectangles).

## LyricsFlow panel
- Both cards are glass (`Ctx::glass` in `Panel.cpp`) with a 6-layer neon purple glow.
- **Bug fixed**: `Ctx::round` never set the quad's uv range, so the SOLID shader measured every rounded box as zero
  size and drew nothing - that's why the panel looked like an empty transparent box. Its buttons, toggles and
  sliders now draw.

## Branches / builds
- BF: `clientui/bf` (from lyricsflow/scaffold, merged 6542d0b) -> CI publishes `clientui-builds`
  (DLL + resources/lyricsflow + resources/client).
- RF: `clientui/rf` (from cinematic aa98552) -> `clientui-rf-builds`.
- Merge plan: clientui/bf -> lyricsflow/scaffold, clientui/rf -> cinematic once Fernando is happy.

## Not done yet / ideas
- Wheel: camera swing to face the player (the reference shows the player close up in the middle) - SpatialCamera can do it.
- HUD petal opens the HUD tab (no drag editor exists yet: HUD Layout is sliders). A drag editor is the natural next step.
- No smooth pixel scrolling (rows are culled whole: glyphs can't be clipped). A scissor rect per glyph range would fix it.
- Profiles (save/switch module setups) were in the prototype but have no backend yet.
