# LyricsFlow - credit and licence

Bedrock Forge's lyrics feature (`src/lyrics/`, `resources/lyricsflow/`, the RF glyph pass) is a port of
**LyricsFlow v2.0 by 374m** (Fabric mod for Minecraft Java 1.21.11, discord.gg/LyricsFlow), MIT licence (see `LICENSE`).

- **Permission:** 374m OK'd modifying LyricsFlow for Fernando's **personal use only**. This port is private and is NOT
  distributed. Anything public needs written permission from 374m first (Discord: zulemayeniden).
- Ported from v2.0 only (`LyricsFlow_Update.jar`); not from v1.19 or the interim update jars.
- What is ported vs new is listed in `docs/LYRICSFLOW_SPEC.md`. New work (falling letters, SDF glow/outline/gradient,
  combat-safe mode, the Bedrock/D3D12 renderer path) is original Bedrock Forge code.
- Fonts keep their own licences: `third_party/lyricsflow/fonts/` (SIL OFL 1.1 for all bundled TTFs, Yellowtail under
  Apache 2.0, DejaVu for the Arabic fallback).

## Panel icons (added 2026-10-30)
`tools/LyricsPanel/icons/*.png` (play, pause, prev, next, refresh, music, close, tab icons, Spotify / YouTube logos) are
copied from LyricsFlow v2.0 (`assets/lyricsflow/textures/gui`, 374m, MIT, see LICENSE) and packed into
`resources/lyricsflow/ui/ui_atlas.png` together with procedural sky thumbnails and preview renders made for this port.
The panel layout follows a LyricsFlow teaser screenshot shared by Fernando. Personal build, not distributed.
